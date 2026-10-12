// Free camera > Photo glue (photo.h, docs/re/photo.md). Main thread, except photo_worker: the WIC encoder thread, which
// shares only interlocked scalars, the values written before it starts, and the strip buffers a slot's interlocked
// state hands over (the two-thread rule; section 4 of the RE note).

#include "photo.h"
#include "photofolder.h" // photo_folder_dir: where the PNG goes
#include "engine.h"
#include "input.h"
#include "freecam.h"     // freecam_pause_holding, freecam_ray
#include "shot.h"        // shot_hides_overlays
#include "entities.h"    // the player, whose world names the level
#include "canvas.h"
#include "font.h"
#include "esp_layout.h"  // esp_draw_label
#include "config.h"
#include "log.h"
#include "ui/menu.h"     // menu_closing_animation_active
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <psapi.h>       // GetProcessMemoryInfo is K32GetProcessMemoryInfo in kernel32 (PSAPI_VERSION 2)
#include <string.h>

// The GUIDs by value, so nothing links windowscodecs.lib or uuid.lib (wincodec.h).
static const GUID kClsidWicFactory = { 0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
static const GUID kPng = { 0x1b7cfaf4, 0x713f, 0x473c, { 0xbb, 0xcd, 0x61, 0x37, 0x42, 0x5f, 0xae, 0xaf } };
static const GUID kBgr24 = { 0x6fddc324, 0x4e03, 0x4bfe, { 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0c } };

// The level's name: `map` with no argument prints the world's inline string at +0x48 ("current map: %s",
// duke!0x100e3ba3); the world is player+0xE0 (freecam-pause.md section 2).
static const unsigned kWorldName = 0x48;

// THE TILES ARE READ FROM THE COMPOSED SCREEN, NOT FROM THE RENDERER: rend_get_image returns zeros on gl (photo.md
// section 1). Desktop Duplication (dxgi/d3d11) reads the monitor, GDI's screen DC is the fallback. The render's place
// in the client is calibrated with marks before the first tile; none found (a black capture) aborts: no black PNG.
static PhotoSm s_sm = photo_begin(PHOTO_MULT_DEF);
static int s_W, s_H;                 // the last frame's render size
static int s_visW, s_visH;           // the last visible size seen (the readout)
static unsigned s_frameNo;
static int s_capW, s_capH, s_n;      // the capture's render size and multiplier
static PhotoVis s_vis;               // the capture's visible rect
static const void* s_world;          // the level it began in
static float s_baseN, s_baseF, s_baseFov;
static int s_haveBase;
static int s_pushR = -1, s_pushC = -1;   // the tile this frame's push drew
static int s_held;                   // frames the current tile has been pushed
static unsigned s_pushes, s_ddTiles, s_gdiTiles, s_blackTiles, s_ddTimeouts;
static ULONGLONG s_t0;
static unsigned long long s_ours, s_oursPeak;   // bytes we hold: the strips and the GDI bitmap
static unsigned long long s_privPeak;

// A strip buffer: s_chunks chunks of s_chunkRows rows (the last may be shorter), never one block, so a fragmented
// address space still holds it (photo.h). state: SLOT_FREE (main may claim), SLOT_FILLING (main writes tiles),
// SLOT_QUEUED (handed to the worker, which encodes it and sets it FREE). Only the owner of the state touches the bytes.
enum { SLOT_FREE = 0, SLOT_FILLING = 1, SLOT_QUEUED = 2 };
struct Slot { unsigned char* chunk[PHOTO_CHUNKS_MAX]; volatile LONG state; volatile LONG row; };
static Slot s_slots[2];
static int s_fill = -1;
static unsigned s_stripStride, s_stripBytes;
static int s_stripRows, s_chunkRows, s_chunks;   // set in prepare, before the worker starts and before any chunk
static int s_secondRefused;

static HANDLE s_thread, s_wake;
static volatile LONG s_wStop, s_wFailed, s_wDone, s_wStrips;
static volatile LONG64 s_wBytes;     // written once by the worker before it sets s_wDone
static wchar_t s_path[MAX_PATH];     // written before CreateThread, read by the worker
static char s_pathUtf8[MAX_PATH * 3];
static int s_closing;                // finished or aborted; buffers freed once the worker has exited

static unsigned long long private_bytes(void) {
  PROCESS_MEMORY_COUNTERS_EX pmc;
  memset(&pmc, 0, sizeof pmc);
  pmc.cb = sizeof pmc;
  if (!GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof pmc)) return 0;
  return pmc.PrivateUsage;
}

static void note_private(void) {
  const unsigned long long p = private_bytes();
  if (p > s_privPeak) s_privPeak = p;
}

static unsigned char* big_alloc(unsigned bytes) {
  unsigned char* p = (unsigned char*)VirtualAlloc(NULL, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (p) { s_ours += bytes; if (s_ours > s_oursPeak) s_oursPeak = s_ours; }
  return p;
}

static void big_free(unsigned char** p, unsigned bytes) {
  if (!*p) return;
  VirtualFree(*p, 0, MEM_RELEASE);
  *p = 0;
  s_ours -= bytes;
}

static unsigned chunk_bytes(int k) { return (unsigned)photo_chunk_rows_in(k, s_stripRows, s_chunkRows) * s_stripStride; }

static void slot_free(Slot* sl) {
  for (int k = 0; k < PHOTO_CHUNKS_MAX; ++k) big_free(&sl->chunk[k], chunk_bytes(k));
}

// Every chunk of a strip, or none: what was got is freed and *refused names the chunk that was not.
static bool slot_alloc(Slot* sl, int* refused) {
  for (int k = 0; k < s_chunks; ++k) {
    sl->chunk[k] = big_alloc(chunk_bytes(k));
    if (!sl->chunk[k]) { *refused = k; slot_free(sl); return false; }
  }
  return true;
}

// The largest free block of the address space, once per photo: a refused chunk is then diagnosable from the log.
static void log_address_space(void) {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  const unsigned char* p = (const unsigned char*)si.lpMinimumApplicationAddress;
  const unsigned char* end = (const unsigned char*)si.lpMaximumApplicationAddress;
  unsigned long long largest = 0, total = 0;
  unsigned regions = 0;
  const ULONGLONG t = GetTickCount64();
  MEMORY_BASIC_INFORMATION mbi;
  while (p < end && VirtualQuery(p, &mbi, sizeof mbi) == sizeof mbi) {
    regions++;
    if (mbi.State == MEM_FREE) { total += mbi.RegionSize; if (mbi.RegionSize > largest) largest = mbi.RegionSize; }
    const unsigned char* next = (const unsigned char*)mbi.BaseAddress + mbi.RegionSize;
    if (next <= p) break;   // wrapped at the top of a 32-bit space
    p = next;
  }
  np_log("photo: address space: largest free block %llu MB, %llu MB free in all, %u regions walked in %llu ms",
         largest >> 20, total >> 20, regions, (unsigned long long)(GetTickCount64() - t));
}

// ---------------------------------------------------------------------------------------------- the encoder
typedef HRESULT (WINAPI* CoInitExFn)(LPVOID, DWORD);
typedef void (WINAPI* CoUninitFn)(void);
typedef HRESULT (WINAPI* CoCreateFn)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
typedef HRESULT (WINAPI* StreamOnFileFn)(LPCWSTR, DWORD, DWORD, BOOL, IStream*, IStream**);

// System32 only: a wrapper DLL of the same name in the game folder (dxgi.dll is a common one) is never picked up.
static HMODULE sys_dll(const wchar_t* name) { return LoadLibraryExW(name, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32); }

static void worker_fail(const char* what, HRESULT hr) {
  np_log("photo: encoder: %s failed (hr 0x%08lX)", what, (unsigned long)hr);
  InterlockedExchange(&s_wFailed, 1);
}

// The queued strip for `row`, or -1. Main queues rows in order, so at most one matches.
static int queued_slot(int row) {
  for (int i = 0; i < 2; ++i)
    if (s_slots[i].state == SLOT_QUEUED && s_slots[i].row == row) return i;
  return -1;
}

static DWORD WINAPI photo_worker(LPVOID) {
  const int n = s_n, tw = s_vis.w, th = s_stripRows, chunkRows = s_chunkRows, chunks = s_chunks;
  const unsigned stride = s_stripStride;
  HMODULE ole = sys_dll(L"ole32.dll");
  CoInitExFn coInit = ole ? (CoInitExFn)GetProcAddress(ole, "CoInitializeEx") : 0;
  CoUninitFn coUninit = ole ? (CoUninitFn)GetProcAddress(ole, "CoUninitialize") : 0;
  CoCreateFn coCreate = ole ? (CoCreateFn)GetProcAddress(ole, "CoCreateInstance") : 0;
  HMODULE shl = sys_dll(L"shlwapi.dll");
  StreamOnFileFn onFile = shl ? (StreamOnFileFn)GetProcAddress(shl, "SHCreateStreamOnFileEx") : 0;
  IWICImagingFactory* fac = 0;
  IStream* file = 0;
  IWICStream* st = 0;
  IWICBitmapEncoder* enc = 0;
  IWICBitmapFrameEncode* fr = 0;
  IPropertyBag2* bag = 0;
  bool ok = false, coOn = false, opened = false;
  HRESULT hr = E_FAIL;
  do {
    if (!coInit || !coUninit || !coCreate) { worker_fail("ole32 (CoInitializeEx/CoCreateInstance)", E_FAIL); break; }
    if (!onFile) { worker_fail("shlwapi (SHCreateStreamOnFileEx)", E_FAIL); break; }
    hr = coInit(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) { worker_fail("CoInitializeEx", hr); break; }
    coOn = true;
    if (FAILED(hr = coCreate(kClsidWicFactory, NULL, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory),
                             (void**)&fac))) { worker_fail("the WIC factory", hr); break; }
    if (FAILED(hr = fac->CreateStream(&st))) { worker_fail("CreateStream", hr); break; }
    // STGM_FAILIFTHERE with fCreate is CREATE_NEW: a file already at the name is never truncated, and so never deleted.
    if (FAILED(hr = onFile(s_path, STGM_WRITE | STGM_SHARE_DENY_WRITE | STGM_FAILIFTHERE, FILE_ATTRIBUTE_NORMAL, TRUE,
                           NULL, &file))) { worker_fail("creating the file (an existing file is never replaced)", hr); break; }
    opened = true;
    if (FAILED(hr = st->InitializeFromIStream(file))) { worker_fail("opening the file", hr); break; }
    if (FAILED(hr = fac->CreateEncoder(kPng, NULL, &enc))) { worker_fail("the PNG encoder", hr); break; }
    if (FAILED(hr = enc->Initialize(st, WICBitmapEncoderNoCache))) { worker_fail("encoder Initialize", hr); break; }
    if (FAILED(hr = enc->CreateNewFrame(&fr, &bag))) { worker_fail("CreateNewFrame", hr); break; }
    if (FAILED(hr = fr->Initialize(bag))) { worker_fail("frame Initialize", hr); break; }
    if (FAILED(hr = fr->SetSize((UINT)(tw * n), (UINT)(th * n)))) { worker_fail("SetSize", hr); break; }
    WICPixelFormatGUID pf = kBgr24;
    if (FAILED(hr = fr->SetPixelFormat(&pf))) { worker_fail("SetPixelFormat", hr); break; }
    if (memcmp(&pf, &kBgr24, sizeof pf) != 0) { worker_fail("24bppBGR (the encoder offered another format)", E_FAIL); break; }
    bool stopped = false;
    for (int row = 0; row < n && !stopped; ++row) {
      int i = -1;
      // The stop is checked before every strip, not only while waiting: an abort or a quit with both strips
      // queued must not compress another one first.
      while (!(stopped = s_wStop != 0) && (i = queued_slot(row)) < 0) WaitForSingleObject(s_wake, 100);
      if (stopped) break;
      const ULONGLONG t = GetTickCount64();
      // Chunk by chunk: WIC takes consecutive line batches. The stop is checked between chunks too.
      hr = S_OK;
      for (int k = 0; k < chunks && SUCCEEDED(hr) && !(stopped = s_wStop != 0); ++k) {
        const UINT rows = (UINT)photo_chunk_rows_in(k, th, chunkRows);
        hr = fr->WritePixels(rows, stride, rows * stride, s_slots[i].chunk[k]);
      }
      InterlockedExchange(&s_slots[i].state, SLOT_FREE);
      if (FAILED(hr)) { worker_fail("WritePixels", hr); stopped = true; break; }
      if (stopped) break;
      InterlockedIncrement(&s_wStrips);
      np_log("photo: strip %d/%d encoded in %llu ms, process private %llu MB", row + 1, n,
             (unsigned long long)(GetTickCount64() - t), private_bytes() >> 20);
    }
    if (stopped) break;
    if (FAILED(hr = fr->Commit())) { worker_fail("frame Commit", hr); break; }
    if (FAILED(hr = enc->Commit())) { worker_fail("encoder Commit", hr); break; }
    ok = true;
  } while (0);
  if (bag) bag->Release();
  if (fr) fr->Release();
  if (enc) enc->Release();
  if (st) st->Release();
  if (file) file->Release();   // closes the file
  if (fac) fac->Release();
  if (coOn) coUninit();
  if (ok) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    LONG64 size = 0;
    if (GetFileAttributesExW(s_path, GetFileExInfoStandard, &fa))
      size = ((LONG64)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
    InterlockedExchange64(&s_wBytes, size);
    InterlockedExchange(&s_wDone, 1);
  } else if (opened) {
    // Our own partial output: opened is set only once this capture created the file (CREATE_NEW above).
    const BOOL del = DeleteFileW(s_path);
    np_log("photo: encoder stopped; partial file %s (%lu)", del ? "deleted" : "NOT deleted", del ? 0ul : GetLastError());
  }
  if (shl) FreeLibrary(shl);
  if (ole) FreeLibrary(ole);
  return 0;
}

// ---------------------------------------------------------------------------------------------- the screen capture

// Readers of a screen rect: Desktop Duplication of the window's output (opened once per photo), GDI's screen DC as the
// fallback. Each hands the rect's BGRA pixels (top row first) to a consumer, which answers GRAB_OK or GRAB_BLACK
// (black, or for the calibration: marks not found). Main thread only.
enum { GRAB_FAIL = 0, GRAB_BLACK = 1, GRAB_OK = 2 };
typedef int (*Consume)(const unsigned char* px, unsigned pitch, int w, int h, void* ctx);

static PhotoVis s_client;            // the client's on-screen rect at start: the readers are sized for it
static RECT s_mon;                   // its monitor

typedef HRESULT (WINAPI* CreateFactory1Fn)(REFIID, void**);
typedef HRESULT (WINAPI* CreateDeviceFn)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT,
                                         UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
struct Dd {
  HMODULE dxgi, d3d11;
  ID3D11Device* dev;
  ID3D11DeviceContext* ctx;
  IDXGIOutputDuplication* dup;
  ID3D11Texture2D* staging;   // the client rect's size; a tile reads its top-left part
  int left, top;              // the output's desktop origin
};
static Dd s_dd;

static void dd_close(void) {
  if (s_dd.staging) s_dd.staging->Release();
  if (s_dd.dup) s_dd.dup->Release();
  if (s_dd.ctx) s_dd.ctx->Release();
  if (s_dd.dev) s_dd.dev->Release();
  if (s_dd.d3d11) FreeLibrary(s_dd.d3d11);
  if (s_dd.dxgi) FreeLibrary(s_dd.dxgi);
  memset(&s_dd, 0, sizeof s_dd);
}

static bool dd_open(HWND wnd) {
  const char* step = "dxgi.dll";
  HRESULT hr = E_FAIL;
  IDXGIFactory1* fac = 0;
  IDXGIAdapter1* ad = 0;
  IDXGIOutput* out = 0;
  IDXGIOutput1* out1 = 0;
  DXGI_OUTPUT_DESC od;
  memset(&od, 0, sizeof od);
  const HMONITOR mon = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
  do {
    s_dd.dxgi = sys_dll(L"dxgi.dll");
    s_dd.d3d11 = sys_dll(L"d3d11.dll");
    CreateFactory1Fn mk = s_dd.dxgi ? (CreateFactory1Fn)GetProcAddress(s_dd.dxgi, "CreateDXGIFactory1") : 0;
    CreateDeviceFn mkDev = s_dd.d3d11 ? (CreateDeviceFn)GetProcAddress(s_dd.d3d11, "D3D11CreateDevice") : 0;
    if (!mk || !mkDev) break;
    step = "CreateDXGIFactory1";
    if (FAILED(hr = mk(__uuidof(IDXGIFactory1), (void**)&fac))) break;
    step = "the window's output";
    hr = E_FAIL;
    for (UINT a = 0; !out && fac->EnumAdapters1(a, &ad) == S_OK; ++a) {
      for (UINT o = 0; ad->EnumOutputs(o, &out) == S_OK; ++o) {
        if (SUCCEEDED(out->GetDesc(&od)) && od.Monitor == mon) break;
        out->Release();
        out = 0;
      }
      if (!out) { ad->Release(); ad = 0; }
    }
    if (!out) break;
    step = "the output's rotation";
    if (od.Rotation != DXGI_MODE_ROTATION_IDENTITY && od.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED) break;
    step = "D3D11CreateDevice";
    if (FAILED(hr = mkDev(ad, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &s_dd.dev, NULL, &s_dd.ctx)))
      break;
    step = "IDXGIOutput1";
    if (FAILED(hr = out->QueryInterface(__uuidof(IDXGIOutput1), (void**)&out1))) break;
    step = "DuplicateOutput";
    if (FAILED(hr = out1->DuplicateOutput(s_dd.dev, &s_dd.dup))) break;
    DXGI_OUTDUPL_DESC dd;
    s_dd.dup->GetDesc(&dd);
    step = "an 8-bit BGRA desktop (HDR or another format)";
    hr = (HRESULT)dd.ModeDesc.Format;
    if (dd.ModeDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) break;
    D3D11_TEXTURE2D_DESC td;
    memset(&td, 0, sizeof td);
    td.Width = (UINT)s_client.w; td.Height = (UINT)s_client.h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    step = "the staging texture";
    if (FAILED(hr = s_dd.dev->CreateTexture2D(&td, NULL, &s_dd.staging))) break;
    s_dd.left = od.DesktopCoordinates.left;
    s_dd.top = od.DesktopCoordinates.top;
    step = 0;
  } while (0);
  if (out1) out1->Release();
  if (out) out->Release();
  if (ad) ad->Release();
  if (fac) fac->Release();
  if (step) {
    np_log("photo: Desktop Duplication unavailable: %s (0x%08lX); the screen is read through GDI", step,
           (unsigned long)hr);
    dd_close();
    return false;
  }
  np_log("photo: Desktop Duplication on the output at (%d,%d)", s_dd.left, s_dd.top);
  return true;
}

// Drops every composition so far: the next acquire is one made after this call.
static void dd_drain(void) {
  if (!s_dd.dup) return;
  DXGI_OUTDUPL_FRAME_INFO fi;
  IDXGIResource* res = 0;
  const HRESULT hr = s_dd.dup->AcquireNextFrame(0, &fi, &res);
  if (SUCCEEDED(hr)) { if (res) res->Release(); s_dd.dup->ReleaseFrame(); }
  else if (hr == DXGI_ERROR_ACCESS_LOST) { np_log("photo: Desktop Duplication lost access; GDI from here"); dd_close(); }
}

static int dd_read(int sx, int sy, int w, int h, Consume fn, void* ctx) {
  if (!s_dd.dup) return GRAB_FAIL;
  DXGI_OUTDUPL_FRAME_INFO fi;
  IDXGIResource* res = 0;
  HRESULT hr = s_dd.dup->AcquireNextFrame(100, &fi, &res);
  if (hr == DXGI_ERROR_WAIT_TIMEOUT) { s_ddTimeouts++; return GRAB_FAIL; }
  if (FAILED(hr)) {
    np_log("photo: AcquireNextFrame failed (0x%08lX); GDI from here", (unsigned long)hr);
    dd_close();
    return GRAB_FAIL;
  }
  ID3D11Texture2D* tex = 0;
  hr = res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
  if (SUCCEEDED(hr)) {
    D3D11_BOX box = { (UINT)(sx - s_dd.left), (UINT)(sy - s_dd.top), 0, (UINT)(sx - s_dd.left + w),
                      (UINT)(sy - s_dd.top + h), 1 };
    s_dd.ctx->CopySubresourceRegion(s_dd.staging, 0, 0, 0, 0, tex, 0, &box);
    tex->Release();
  }
  res->Release();
  s_dd.dup->ReleaseFrame();
  if (FAILED(hr)) return GRAB_FAIL;
  D3D11_MAPPED_SUBRESOURCE m;
  if (FAILED(s_dd.ctx->Map(s_dd.staging, 0, D3D11_MAP_READ, 0, &m))) return GRAB_FAIL;
  const int r = fn((const unsigned char*)m.pData, m.RowPitch, w, h, ctx);
  s_dd.ctx->Unmap(s_dd.staging, 0);
  return r;
}

// GDI: BitBlt from the screen DC into a top-down 32-bit DIB the client rect's size, kept for the photo.
static HDC s_gdiDc;
static HBITMAP s_gdiBmp, s_gdiOld;
static unsigned char* s_gdiBits;
static unsigned s_gdiBytes;

static void gdi_close(void) {
  if (s_gdiDc) { SelectObject(s_gdiDc, s_gdiOld); DeleteDC(s_gdiDc); s_gdiDc = 0; }
  if (s_gdiBmp) { DeleteObject(s_gdiBmp); s_gdiBmp = 0; s_ours -= s_gdiBytes; }
  s_gdiBits = 0;
}

static int gdi_read(int sx, int sy, int w, int h, Consume fn, void* ctx) {
  HDC scr = GetDC(NULL);
  if (!scr) return GRAB_FAIL;
  if (!s_gdiDc) {
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = s_client.w;
    bi.bmiHeader.biHeight = -s_client.h;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    s_gdiDc = CreateCompatibleDC(scr);
    void* bits = 0;
    s_gdiBmp = s_gdiDc ? CreateDIBSection(s_gdiDc, &bi, DIB_RGB_COLORS, &bits, NULL, 0) : 0;
    if (!s_gdiBmp) { ReleaseDC(NULL, scr); np_log("photo: GDI bitmap refused (%lu)", GetLastError()); gdi_close(); return GRAB_FAIL; }
    s_gdiOld = (HBITMAP)SelectObject(s_gdiDc, s_gdiBmp);
    s_gdiBits = (unsigned char*)bits;
    s_gdiBytes = (unsigned)s_client.w * (unsigned)s_client.h * 4u;
    s_ours += s_gdiBytes;
    if (s_ours > s_oursPeak) s_oursPeak = s_ours;
  }
  const BOOL ok = BitBlt(s_gdiDc, 0, 0, w, h, scr, sx, sy, SRCCOPY);
  ReleaseDC(NULL, scr);
  if (!ok) return GRAB_FAIL;
  GdiFlush();
  return fn(s_gdiBits, (unsigned)s_client.w * 4u, w, h, ctx);
}

// Desktop Duplication, then GDI when it failed or answered black. *via names the reader that answered OK.
static int read_screen(int sx, int sy, int w, int h, Consume fn, void* ctx, const char** via) {
  const int d = dd_read(sx, sy, w, h, fn, ctx);
  if (d == GRAB_OK) { *via = "Desktop Duplication"; return GRAB_OK; }
  const int g = gdi_read(sx, sy, w, h, fn, ctx);
  *via = "GDI";
  if (g == GRAB_OK) return GRAB_OK;
  return (d == GRAB_BLACK || g == GRAB_BLACK) ? GRAB_BLACK : GRAB_FAIL;
}

// A tile into strip column c (ctx: the column). Black is an answer, not a failure: a void corner is black.
static int consume_tile(const unsigned char* px, unsigned pitch, int w, int h, void* ctx) {
  return photo_copy_bgra_chunks(px, pitch, w, h, s_slots[s_fill].chunk, s_chunkRows, s_stripStride, *(const int*)ctx)
             ? GRAB_OK : GRAB_BLACK;
}

static int grab_tile(int c) {
  const char* via = "";
  const int g = read_screen(s_vis.sx, s_vis.sy, s_vis.w, s_vis.h, consume_tile, &c, &via);
  if (g == GRAB_OK) { if (via[0] == 'D') s_ddTiles++; else s_gdiTiles++; }
  return g;
}

// --- The calibration (photo.h): where the render's rows and columns land in the client --------------------------
static int s_calibrated, s_calDrawn;
struct CalFound { double at[4]; };   // band centroids in capture pixels, -1 when not found

static int consume_cal(const unsigned char* px, unsigned pitch, int w, int h, void* ctx) {
  CalFound* f = (CalFound*)ctx;
  f->at[0] = photo_band_centroid(px, pitch, w, h, PHOTO_CAL_ROW1, 0);
  f->at[1] = photo_band_centroid(px, pitch, w, h, PHOTO_CAL_ROW2, 0);
  f->at[2] = photo_band_centroid(px, pitch, w, h, PHOTO_CAL_COL1, 1);
  f->at[3] = photo_band_centroid(px, pitch, w, h, PHOTO_CAL_COL2, 1);
  return f->at[0] >= 0.0 && f->at[1] >= 0.0 && f->at[2] >= 0.0 && f->at[3] >= 0.0 ? GRAB_OK : GRAB_BLACK;
}

// The bands, drawn after everything else on an arming frame (photo_frame); nothing else of ours is on screen then.
static void draw_cal_marks(int W, int H) {
  const int b = PHOTO_CAL_BAND;
  canvas_begin(W, H);
  canvas_rect(0, photo_cal_band_at(H, 0), W, b, 0xFF000000u | PHOTO_CAL_ROW1);
  canvas_rect(0, photo_cal_band_at(H, 1), W, b, 0xFF000000u | PHOTO_CAL_ROW2);
  canvas_rect(photo_cal_band_at(W, 0), 0, b, H, 0xFF000000u | PHOTO_CAL_COL1);
  canvas_rect(photo_cal_band_at(W, 1), 0, b, H, 0xFF000000u | PHOTO_CAL_COL2);
  canvas_end();
}

// Reads the client with the bands on it. True, with s_vis the visible part of the render, only when the render lands
// 1:1 (with any offset). A stretched render is refused rather than resampled: a resampled tile is not the render's
// pixels, and the seams would blur (a deliberate choice).
static bool calibrate(int W, int H) {
  CalFound f = { { -1.0, -1.0, -1.0, -1.0 } };
  const char* via = "";
  const int g = read_screen(s_client.sx, s_client.sy, s_client.w, s_client.h, consume_cal, &f, &via);
  int ox = 0, oy = 0;
  const double r1 = photo_cal_band_centre(H, 0), r2 = photo_cal_band_centre(H, 1);
  const double c1 = photo_cal_band_centre(W, 0), c2 = photo_cal_band_centre(W, 1);
  const bool rows = photo_cal_axis(r1, r2, f.at[0], f.at[1], &oy);
  const bool cols = photo_cal_axis(c1, c2, f.at[2], f.at[3], &ox);
  np_log("photo: calibration via %s of the %dx%d client rect at screen (%d,%d): row bands centred at render %.1f, %.1f "
         "measured at capture %.3f, %.3f (distance off by %+.3f); column bands %.1f, %.1f at %.3f, %.3f (off by %+.3f)",
         g == GRAB_FAIL ? "nothing (both readers failed)" : via, s_client.w, s_client.h, s_client.sx, s_client.sy, r1, r2,
         f.at[0], f.at[1], (f.at[1] - f.at[0]) - (r2 - r1), c1, c2, f.at[2], f.at[3], (f.at[3] - f.at[2]) - (c2 - c1));
  if (g != GRAB_OK || !rows || !cols) {
    np_log("photo: calibration: %s; ABORTING (a stretched render is not resampled)",
           g != GRAB_OK ? "the bands were not all found" : "the render is NOT 1:1 on screen (off by 0.5 px or more)");
    return false;
  }
  const int rx = s_client.sx + ox, ry = s_client.sy + oy;   // render (0,0) on screen
  if (!photo_visible(rx, ry, W, H, s_mon.left, s_mon.top, s_mon.right, s_mon.bottom, &s_vis)) {
    np_log("photo: calibration: the render at screen (%d,%d) is off its monitor", rx, ry);
    return false;
  }
  np_log("photo: calibration: render 1:1 with its (0,0) at screen (%d,%d), client offset (%d,%d); visible render "
         "rect %dx%d from render (%d,%d)", rx, ry, ox, oy, s_vis.w, s_vis.h, s_vis.vx, s_vis.vy);
  return true;
}

// ---------------------------------------------------------------------------------------------- main thread
static void free_all(void) {
  for (int i = 0; i < 2; ++i) { slot_free(&s_slots[i]); s_slots[i].state = SLOT_FREE; }
  s_fill = -1;
  if (s_thread) { CloseHandle(s_thread); s_thread = 0; }
  if (s_wake) { CloseHandle(s_wake); s_wake = 0; }
  s_closing = 0;
}

// The buffers go only once the worker has exited: it may be inside WritePixels on one of them.
static void poll_cleanup(void) {
  if (!s_closing) return;
  if (s_thread && WaitForSingleObject(s_thread, 0) != WAIT_OBJECT_0) return;
  free_all();
}

static void release(bool stopWorker) {
  dd_close();
  gdi_close();
  if (stopWorker) InterlockedExchange(&s_wStop, 1);
  if (s_wake) SetEvent(s_wake);
  s_closing = 1;
  poll_cleanup();
}

static const void* player_world(void) {
  const unsigned char* player = (const unsigned char*)entities_snapshot().player;
  const void* w = 0;
  if (player) memcpy(&w, player + 0xE0, sizeof w);
  return w;
}

// The client's on-screen rect and its monitor. The client is the render size, or one row taller: our borderless
// window (display.md) is H+1 rows so GLDirect stays windowed. Anything else (a scaled or DPI-virtualised window) is
// refused: the screen would not hold the render's pixels. Where the render sits in it is calibrated.
static bool window_client(int W, int H, PhotoVis* v, RECT* mon) {
  const HWND wnd = input_window();
  RECT cr;
  POINT o = { 0, 0 };
  MONITORINFO mi;
  mi.cbSize = sizeof mi;
  if (!wnd || !GetClientRect(wnd, &cr) || !ClientToScreen(wnd, &o) ||
      !GetMonitorInfoW(MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST), &mi))
    return false;
  if (cr.right != W || (cr.bottom != H && cr.bottom != H + 1)) return false;
  if (mon) *mon = mi.rcMonitor;
  return photo_visible(o.x, o.y, cr.right, cr.bottom, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right,
                       mi.rcMonitor.bottom, v);
}

// The world's inline name, or "" when it is not a short printable string.
static void level_name(const void* world, char* out, int cap) {
  out[0] = 0;
  const unsigned char* p = (const unsigned char*)world + kWorldName;
  if (!world || !guarded_readable(p, 64)) return;
  int n = 0;
  while (n < 63 && p[n] >= 0x20 && p[n] < 0x7F) n++;
  if (p[n] != 0 || n >= cap) return;
  memcpy(out, p, (size_t)n);
  out[n] = 0;
}

// <Photo folder>\<name>, the folder created (photofolder.h: the configured one, else <Pictures>\Nuke Panel), with
// "-n" while a file already has the name. Built once at the capture's start: a folder chosen later does not move a
// photo in progress. False (logged) without one.
static bool build_path(const char* name) {
  wchar_t dir[MAX_PATH], wname[160];
  if (!photo_folder_dir(dir, MAX_PATH)) return false;   // logged there
  const size_t n = wcslen(dir);
  const bool sep = n && (dir[n - 1] == L'\\' || dir[n - 1] == L'/');   // a drive root ends in one
  char numbered[160];
  for (int i = 1; i <= PHOTO_NAME_TRIES; ++i) {
    const bool ok = photo_file_name_n(name, i, numbered, sizeof numbered) > 0 &&
                    MultiByteToWideChar(CP_ACP, 0, numbered, -1, wname, 160) > 0 &&
                    _snwprintf_s(s_path, MAX_PATH, _TRUNCATE, sep ? L"%s%s" : L"%s\\%s", dir, wname) > 0;
    if (!ok) { np_log("photo: the photo's path is over %d characters", MAX_PATH - 1); return false; }
    if (GetFileAttributesW(s_path) != INVALID_FILE_ATTRIBUTES) continue;   // the worker would refuse it anyway
    WideCharToMultiByte(CP_UTF8, 0, s_path, -1, s_pathUtf8, sizeof s_pathUtf8, NULL, NULL);
    return true;
  }
  np_log("photo: %s and its -2..-%d names are all taken", name, (int)PHOTO_NAME_TRIES);
  return false;
}

// At the key: the client and its readers. The strips, the file and the encoder wait for the calibration (prepare).
static bool start(int W, int H) {
  s_capW = W; s_capH = H; s_n = s_sm.n;
  s_world = player_world();
  s_haveBase = 0;
  s_pushR = s_pushC = -1;
  s_held = 0;
  s_calibrated = s_calDrawn = 0;
  s_pushes = s_ddTiles = s_gdiTiles = s_blackTiles = s_ddTimeouts = 0;
  s_secondRefused = 0;
  s_oursPeak = s_ours;
  s_privPeak = 0;
  s_t0 = GetTickCount64();
  if (!window_client(W, H, &s_client, &s_mon)) {
    np_log("photo: the game window's client is neither the %dx%d render nor one row taller, or is off its monitor", W, H);
    return false;
  }
  log_address_space();
  dd_open(input_window());   // GDI stands in when it is refused (logged)
  return true;
}

// After the calibration: the strips for the visible render rect, the file, the encoder.
static bool prepare(void) {
  const unsigned long long strip = (unsigned long long)photo_stride(s_vis.w * s_n) * (unsigned long long)s_vis.h;
  if (strip > 0x60000000ull) { np_log("photo: a %llu MB strip is too large for this process", strip >> 20); return false; }
  s_stripStride = photo_stride(s_vis.w * s_n);
  s_stripBytes = (unsigned)strip;
  s_stripRows = s_vis.h;
  s_chunkRows = photo_chunk_rows(s_stripStride, s_stripRows);
  s_chunks = photo_chunk_count(s_stripRows, s_chunkRows);
  np_log("photo: a %u MB strip in %d chunks of %d rows (%u MB each, the last %d rows)", s_stripBytes >> 20, s_chunks,
         s_chunkRows, chunk_bytes(0) >> 20, photo_chunk_rows_in(s_chunks - 1, s_stripRows, s_chunkRows));
  int refused = -1;
  if (!slot_alloc(&s_slots[0], &refused)) {
    np_log("photo: no memory for a %u MB strip: chunk %d/%d of %u MB refused", s_stripBytes >> 20, refused + 1, s_chunks,
           chunk_bytes(refused) >> 20);
    return false;
  }
  s_slots[0].state = SLOT_FILLING;
  s_fill = 0;
  char level[64], name[128];
  level_name(s_world, level, sizeof level);
  SYSTEMTIME t;
  GetLocalTime(&t);
  if (photo_file_name(name, sizeof name, level, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                      s_vis.w * s_n, s_vis.h * s_n) < 0 || !build_path(name))
    return false;
  s_wStop = s_wFailed = s_wDone = s_wStrips = 0;
  s_wBytes = 0;
  s_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
  s_thread = s_wake ? CreateThread(NULL, 0, photo_worker, NULL, 0, NULL) : 0;
  if (!s_thread) { np_log("photo: the encoder thread did not start (%lu)", GetLastError()); return false; }
  char size[48], shown[sizeof s_pathUtf8];
  photo_size_text(s_n, s_vis.w, s_vis.h, size, sizeof size);
  np_log("photo: start %s from the %dx%d render's visible %dx%d at render (%d,%d), %d tiles in %d strips of %u MB "
         "(at most 2 held), level \"%s\", to %s", size, s_capW, s_capH, s_vis.w, s_vis.h, s_vis.vx, s_vis.vy,
         s_n * s_n, s_n, s_stripBytes >> 20, level, np_log_path(s_pathUtf8, shown, sizeof shown));
  return true;
}

// A free strip buffer for the next row (the second allocated on first need), or -1.
static int free_slot(void) {
  for (int i = 0; i < 2; ++i)
    if (s_slots[i].chunk[0] && s_slots[i].state == SLOT_FREE) return i;
  for (int i = 0; i < 2; ++i) {
    if (s_slots[i].chunk[0] || s_secondRefused) continue;
    int refused = -1;
    if (slot_alloc(&s_slots[i], &refused)) { s_slots[i].state = SLOT_FREE; return i; }
    s_secondRefused = 1;
    np_log("photo: a second %u MB strip was refused at chunk %d/%d; one strip in flight, each row waits for the "
           "encoder", s_stripBytes >> 20, refused + 1, s_chunks);
  }
  return -1;
}

void photo_capture(int W, int H) {
  s_W = W; s_H = H;
  // The readout's visible size (the client's visible rows), refreshed twice a second while nothing is captured.
  if (s_sm.phase == PH_IDLE && s_frameNo++ % 30 == 0) {
    PhotoVis v;
    if (window_client(W, H, &v, 0)) { s_visW = v.w; s_visH = v.h < H ? v.h : H; }
    else { s_visW = W; s_visH = H; }
  }
}

void photo_push(float view[16], float inv[16]) {
  if (s_sm.phase == PH_ARMING && g_eng.rend_projection_get) {
    // The view the player sees, after nocull's paused re-apply: every tile narrows this one.
    g_eng.rend_projection_get(&s_baseN, &s_baseF, &s_baseFov);
    s_haveBase = s_baseN > 0.f && s_baseFov > 0.f && s_baseFov < 180.f;
    return;
  }
  if (s_sm.phase != PH_TILE || !s_haveBase || !g_eng.rend_projection_change || !g_eng.rend_get_proj_matrix) return;
  g_eng.rend_projection_change(s_baseN, s_baseF, photo_tile_fov(s_baseFov, s_n));
  float p[16];
  g_eng.rend_get_proj_matrix(p);   // the engine's own m[0], m[5] for the narrowed fov
  if (!(p[0] > 0.f) || !(p[5] > 0.f)) return;
  float ox, oy, tv[16], ti[16];
  photo_tile_offsets(s_n, s_sm.r, s_sm.c, s_capW, s_capH, s_vis, &ox, &oy);
  photo_tile_matrices(view, inv, p[0], p[5], ox, oy, tv, ti);
  memcpy(view, tv, sizeof tv);
  memcpy(inv, ti, sizeof ti);
  s_pushR = s_sm.r; s_pushC = s_sm.c;
  s_pushes++;
}

static void draw_progress(int W, int H) {
  char line[96];
  if (s_sm.phase == PH_DRAIN)
    _snprintf_s(line, sizeof line, _TRUNCATE, "Photo: writing the PNG, strip %ld/%d", s_wStrips, s_n);
  else _snprintf_s(line, sizeof line, _TRUNCATE, "Photo %d/%d", s_sm.r * s_n, s_n * s_n);
  canvas_begin(W, H);
  esp_draw_label(FREECAM_READOUT_MARGIN, FREECAM_READOUT_MARGIN, line, FREECAM_READOUT_RGBA, FONT_SMALL);
  canvas_end();
}

void photo_frame(int W, int H) {
  poll_cleanup();
  const int presses = input_photo_take();
  if (s_sm.phase == PH_IDLE) {
    if (!presses) return;
    if (s_closing) { np_log("photo: the last photo's encoder is still closing; key ignored"); return; }
    if (!player_world()) { np_log("photo: no level under the camera; key ignored"); return; }
    s_sm.n = photo_mult_clamped(g_cfg.photoMult);
  }
  PhotoIn in;
  memset(&in, 0, sizeof in);
  float eye[3], fwd[3];
  in.press = presses > 0;
  in.cam = freecam_ray(eye, fwd) ? 1 : 0;
  in.panel = input_panel_open();
  const void* world = player_world();
  in.world = world != 0 && (s_sm.phase == PH_IDLE || world == s_world);
  in.screen = input_console_open() || input_ui_window("screen", "menu_main") || input_ui_window("screen", "menu_help");
  const HWND wnd = input_window();
  in.focus = wnd && GetForegroundWindow() == wnd && !IsIconic(wnd);
  PhotoVis now;
  in.sameSize = s_sm.phase == PH_IDLE || (W == s_capW && H == s_capH && window_client(W, H, &now, 0) &&
                                          memcmp(&now, &s_client, sizeof now) == 0);
  in.frozen = freecam_pause_holding();
  in.hidden = shot_hides_overlays() && !menu_closing_animation_active();
  int calFailed = 0;
  // The calibration marks have been on screen long enough (drawn below on earlier arming frames): read them.
  if (s_sm.phase == PH_ARMING && !s_calibrated && s_calDrawn > 0) {
    if (s_calDrawn == PHOTO_HOLD_DRAIN) dd_drain();
    if (s_calDrawn >= PHOTO_HOLD_CAPTURE && in.cam && in.focus && in.sameSize) {
      if (calibrate(W, H)) {
        if (prepare()) s_calibrated = 1;
        else calFailed = PHR_START;
      } else {
        calFailed = PHR_CALIBRATION;
      }
    }
  }
  in.calibrated = s_calibrated;
  // The screen shows this tile once it has been pushed for PHOTO_HOLD_CAPTURE frames (photo.h).
  int black = 0;
  if (s_sm.phase == PH_TILE && s_pushR == s_sm.r && s_pushC == s_sm.c) {
    ++s_held;
    if (s_held == PHOTO_HOLD_DRAIN) dd_drain();
    if (s_held >= PHOTO_HOLD_CAPTURE && in.cam && in.focus && in.sameSize) {
      const int g = grab_tile(s_sm.c);
      in.tileRead = g != GRAB_FAIL;
      black = g == GRAB_BLACK;
    }
  }
  s_pushR = s_pushC = -1;
  const int waiting = s_sm.phase == PH_WAIT;
  const int slot = waiting ? free_slot() : -1;
  in.bufferFree = slot >= 0;
  in.encFailed = s_wFailed != 0;
  in.encDone = s_wDone != 0;
  const int prevPhase = s_sm.phase, prevR = s_sm.r, prevC = s_sm.c;
  const int tilesDone = s_sm.r * s_sm.n + s_sm.c;
  int act = calFailed ? photo_fail(&s_sm, calFailed) : photo_step(&s_sm, in);
  if (black && act != PA_ABORT) s_blackTiles++;
  if (s_sm.phase != prevPhase || s_sm.r != prevR || s_sm.c != prevC) s_held = 0;
  if (waiting && s_sm.phase == PH_TILE) {
    s_fill = slot;
    InterlockedExchange(&s_slots[slot].state, SLOT_FILLING);
  }
  note_private();
  if (act == PA_START) {
    if (!start(W, H)) {
      photo_fail(&s_sm, PHR_START);
      np_log("photo: ABORTED: %s", photo_why_text(s_sm.reason));
      release(true);
      return;
    }
  } else if (act == PA_FLUSH) {
    Slot& sl = s_slots[s_fill];
    InterlockedExchange(&sl.row, s_sm.flushRow);
    InterlockedExchange(&sl.state, SLOT_QUEUED);
    SetEvent(s_wake);
    s_fill = -1;
  } else if (act == PA_FINISH) {
    char shown[sizeof s_pathUtf8];
    np_log_path(s_pathUtf8, shown, sizeof shown);
    np_log("photo: done in %.1f s, %lld bytes, tiles from Desktop Duplication %u, GDI %u, black %u (dup timeouts %u), "
           "peak held %llu MB, process private peak %llu MB, %u tile pushes: %s",
           (double)(GetTickCount64() - s_t0) / 1000.0, (long long)s_wBytes, s_ddTiles, s_gdiTiles, s_blackTiles,
           s_ddTimeouts, s_oursPeak >> 20, s_privPeak >> 20, s_pushes, shown);
    release(false);
    return;
  } else if (act == PA_ABORT) {
    np_log("photo: ABORTED after %.1f s at tile %d/%d: %s; tiles from Desktop Duplication %u, GDI %u; peak held %llu MB",
           (double)(GetTickCount64() - s_t0) / 1000.0, tilesDone, s_n * s_n, photo_why_text(s_sm.reason), s_ddTiles,
           s_gdiTiles, s_oursPeak >> 20);
    release(true);
    return;
  }
  // Arming, frozen and hidden: the calibration marks go on screen (nothing else of ours is drawn while arming).
  if (s_sm.phase == PH_ARMING && !s_calibrated) {
    if (in.frozen && in.hidden) { draw_cal_marks(W, H); s_calDrawn++; }
    else s_calDrawn = 0;
  }
  // Between rows only: the capture reads the screen, so nothing of ours may be on it while a tile is up.
  if (s_sm.phase == PH_WAIT || s_sm.phase == PH_DRAIN) draw_progress(W, H);
}

bool photo_busy(void) { return s_sm.phase != PH_IDLE; }
bool photo_capturing(void) { return s_sm.phase != PH_IDLE && s_sm.phase != PH_DRAIN; }
void photo_screen_size(int* W, int* H) { *W = s_visW; *H = s_visH; }

void photo_restore_for_quit(void) {
  dd_close();
  gdi_close();
  if (!s_thread) return;
  const bool done = s_wDone != 0;
  InterlockedExchange(&s_wStop, 1);
  if (s_wake) SetEvent(s_wake);
  // Bounded: the worker checks the stop before every strip; one WritePixels is seconds at most.
  const DWORD w = WaitForSingleObject(s_thread, 5000);
  np_log("photo: quit with the encoder %s; it %s", done ? "done" : "running",
         w == WAIT_OBJECT_0 ? "has exited" : "did not exit within 5 s (its partial file may remain)");
  s_sm.phase = PH_IDLE;
}
