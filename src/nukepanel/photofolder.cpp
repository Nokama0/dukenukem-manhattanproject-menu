// Free camera > Photo folder glue (photofolder.h; user 2026-10-03). Main thread only. ole32 and shell32 are loaded at
// run time from System32, as photo.cpp loads them: the build links kernel32, user32 and gdi32 only.
#include "photofolder.h"
#include "config.h"
#include "input.h"
#include "log.h"
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>

// By value, so nothing links uuid.lib (KnownFolders.h, ShObjIdl.h).
static const GUID kFolderPictures = { 0x33E28130, 0x4E1E, 0x4676, { 0x83, 0x5A, 0x98, 0x39, 0x5C, 0x3B, 0xC3, 0xBB } };
static const GUID kClsidFileOpenDialog = { 0xDC1C5A9C, 0xE88A, 0x4DDE, { 0xA5, 0xA1, 0x60, 0xF8, 0x2A, 0x20, 0xAE, 0xF7 } };

typedef HRESULT (WINAPI* KnownFolderFn)(REFGUID, DWORD, HANDLE, PWSTR*);
typedef void (WINAPI* TaskFreeFn)(LPVOID);
typedef int (WINAPI* MkDirsFn)(HWND, LPCWSTR, const SECURITY_ATTRIBUTES*);
typedef HRESULT (WINAPI* CoInitExFn)(LPVOID, DWORD);
typedef void (WINAPI* CoUninitFn)(void);
typedef HRESULT (WINAPI* CoCreateFn)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
typedef HRESULT (WINAPI* ItemFromPathFn)(PCWSTR, IBindCtx*, REFIID, void**);

// <Pictures>\Nuke Panel, looked up once: the row asks every frame it is drawn.
static wchar_t s_def[MAX_PATH];
static char s_defUtf8[CONFIG_STR_CAP];
static int s_defTried, s_defOk;
static bool s_request;

// System32 only: a DLL of the same name beside the game is never picked up.
static HMODULE sys_dll(const wchar_t* name) { return LoadLibraryExW(name, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32); }

static void to_utf8(const wchar_t* w, char* out, int cap) {
  if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, NULL, NULL)) out[0] = 0;
}

static bool default_dir(wchar_t* out, int cap) {
  if (!s_defTried) {
    s_defTried = 1;
    HMODULE sh = sys_dll(L"shell32.dll"), ole = sys_dll(L"ole32.dll");
    KnownFolderFn known = sh ? (KnownFolderFn)GetProcAddress(sh, "SHGetKnownFolderPath") : 0;
    TaskFreeFn taskFree = ole ? (TaskFreeFn)GetProcAddress(ole, "CoTaskMemFree") : 0;
    PWSTR pics = 0;
    const HRESULT hr = known && taskFree ? known(kFolderPictures, 0, NULL, &pics) : E_FAIL;
    s_defOk = SUCCEEDED(hr) && pics && _snwprintf_s(s_def, MAX_PATH, _TRUNCATE, L"%s\\Nuke Panel", pics) > 0;
    if (pics) taskFree(pics);
    if (sh) FreeLibrary(sh);
    if (ole) FreeLibrary(ole);
    if (s_defOk) to_utf8(s_def, s_defUtf8, sizeof s_defUtf8);
    else np_log("photo: the Pictures folder is unknown (hr 0x%08lX)", (unsigned long)hr);
  }
  return s_defOk && wcscpy_s(out, (size_t)cap, s_def) == 0;
}

static bool is_dir(const wchar_t* p) {
  const DWORD a = GetFileAttributesW(p);
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// The folder, created with any missing parents when absent; *err the failure's code.
static bool ensure_dir(const wchar_t* p, DWORD* err) {
  *err = 0;
  if (is_dir(p)) return true;
  HMODULE sh = sys_dll(L"shell32.dll");
  MkDirsFn mk = sh ? (MkDirsFn)GetProcAddress(sh, "SHCreateDirectoryExW") : 0;
  if (mk) *err = (DWORD)mk(NULL, p, NULL);
  else if (!CreateDirectoryW(p, NULL)) *err = GetLastError();
  if (sh) FreeLibrary(sh);
  return is_dir(p);
}

// The configured folder in UTF-16; false (logged) when it is not a drive-letter path, not valid UTF-8 or too long.
static bool configured_dir(wchar_t* out, int cap) {
  if (photo_folder_is_default(g_cfg.photoFolder)) return false;
  char shown[CONFIG_STR_CAP];
  if (!photo_folder_path_ok(g_cfg.photoFolder)) {
    np_log("photo: the Photo folder \"%s\" is not a drive-letter path (a letter, a colon, a backslash); the default instead",
           np_log_path(g_cfg.photoFolder, shown, sizeof shown));
    return false;
  }
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, g_cfg.photoFolder, -1, out, cap) > 0) return true;
  const DWORD e = GetLastError();
  np_log("photo: the Photo folder \"%s\" is not a valid UTF-8 path (%lu)",
         np_log_path(g_cfg.photoFolder, shown, sizeof shown), e);
  return false;
}

bool photo_folder_dir(wchar_t* out, int cap) {
  DWORD err = 0;
  char shown[CONFIG_STR_CAP];
  if (configured_dir(out, cap)) {
    if (ensure_dir(out, &err)) return true;
    np_log("photo: the Photo folder %s could not be created (%lu); the default instead",
           np_log_path(g_cfg.photoFolder, shown, sizeof shown), err);
  }
  if (!default_dir(out, cap)) return false;
  if (ensure_dir(out, &err)) return true;
  np_log("photo: the default folder %s could not be created (%lu)", np_log_path(s_defUtf8, shown, sizeof shown), err);
  return false;
}

void photo_folder_shown(char* out, int cap, bool* isDefault) {
  // A refused path shows as the default, where photos go.
  *isDefault = photo_folder_is_default(g_cfg.photoFolder) || !photo_folder_path_ok(g_cfg.photoFolder);
  wchar_t d[MAX_PATH];
  if (!*isDefault) strcpy_s(out, (size_t)cap, g_cfg.photoFolder);
  else if (default_dir(d, MAX_PATH)) strcpy_s(out, (size_t)cap, s_defUtf8);
  else strcpy_s(out, (size_t)cap, "Pictures\\Nuke Panel");
}

void photo_folder_request(void) { s_request = true; }

// The chosen folder into g_cfg: the default itself is stored as "" so the row keeps it dim; a path too long for the
// ini key is refused rather than cut.
static void store(const wchar_t* chosen) {
  char utf8[CONFIG_STR_CAP];
  wchar_t def[MAX_PATH];
  const bool isDef = default_dir(def, MAX_PATH) && _wcsicmp(chosen, def) == 0;
  if (isDef) utf8[0] = 0;
  else if (!WideCharToMultiByte(CP_UTF8, 0, chosen, -1, utf8, sizeof utf8, NULL, NULL)) {
    np_log("photo folder: the chosen path is over %d bytes as UTF-8; unchanged", CONFIG_STR_CAP - 1);
    return;
  }
  char shown[CONFIG_STR_CAP];
  if (!isDef && !photo_folder_path_ok(utf8)) {   // a network location picked in the dialog
    np_log("photo folder: %s is not a drive-letter path (a letter, a colon, a backslash); unchanged", np_log_path(utf8, shown, sizeof shown));
    return;
  }
  if (strcmp(utf8, g_cfg.photoFolder) != 0) {
    strcpy_s(g_cfg.photoFolder, sizeof g_cfg.photoFolder, utf8);
    g_cfgDirty = 1;
  }
  np_log("photo folder: %s%s", isDef ? "the default, " : "", np_log_path(isDef ? s_defUtf8 : utf8, shown, sizeof shown));
}

// IFileOpenDialog with FOS_PICKFOLDERS, owned by the game window, starting in the folder photos go to now.
static void pick(void) {
  HMODULE ole = sys_dll(L"ole32.dll"), sh = sys_dll(L"shell32.dll");
  CoInitExFn coInit = ole ? (CoInitExFn)GetProcAddress(ole, "CoInitializeEx") : 0;
  CoUninitFn coUninit = ole ? (CoUninitFn)GetProcAddress(ole, "CoUninitialize") : 0;
  CoCreateFn coCreate = ole ? (CoCreateFn)GetProcAddress(ole, "CoCreateInstance") : 0;
  TaskFreeFn taskFree = ole ? (TaskFreeFn)GetProcAddress(ole, "CoTaskMemFree") : 0;
  ItemFromPathFn itemFrom = sh ? (ItemFromPathFn)GetProcAddress(sh, "SHCreateItemFromParsingName") : 0;
  IFileOpenDialog* dlg = 0;
  IShellItem* start = 0;
  IShellItem* got = 0;
  PWSTR chosen = 0;
  bool coOn = false, cancelled = false;
  HRESULT hr = E_FAIL;
  const char* step = "ole32/shell32 exports";
  do {
    if (!coInit || !coUninit || !coCreate || !taskFree) break;
    step = "CoInitializeEx";
    hr = coInit(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    // S_OK and S_FALSE each took a reference this call must give back; RPC_E_CHANGED_MODE (already multithreaded
    // here) took none, and the dialog is tried in that apartment.
    if (SUCCEEDED(hr)) coOn = true;
    else if (hr == RPC_E_CHANGED_MODE) np_log("photo folder: this thread is in the multithreaded apartment already");
    else break;
    step = "the folder dialog";
    if (FAILED(hr = coCreate(kClsidFileOpenDialog, NULL, CLSCTX_INPROC_SERVER, __uuidof(IFileOpenDialog),
                             (void**)&dlg))) break;
    FILEOPENDIALOGOPTIONS opts = 0;
    dlg->GetOptions(&opts);
    step = "SetOptions";
    if (FAILED(hr = dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST))) break;
    dlg->SetTitle(L"Photo folder");
    // A configured folder not created yet cannot be opened: then the default, then the dialog's own choice.
    wchar_t dir[MAX_PATH];
    const bool haveDir = (configured_dir(dir, MAX_PATH) && is_dir(dir)) || (default_dir(dir, MAX_PATH) && is_dir(dir));
    if (haveDir && itemFrom && SUCCEEDED(itemFrom(dir, NULL, __uuidof(IShellItem), (void**)&start))) dlg->SetFolder(start);
    input_modal_begin();
    hr = dlg->Show(input_window());
    input_modal_end();
    step = "Show";
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) { cancelled = true; break; }
    if (FAILED(hr)) break;
    step = "GetResult";
    if (FAILED(hr = dlg->GetResult(&got))) break;
    step = "the folder's file system path";
    if (FAILED(hr = got->GetDisplayName(SIGDN_FILESYSPATH, &chosen))) break;
    step = 0;
  } while (0);
  if (chosen) store(chosen);
  else if (cancelled) np_log("photo folder: cancelled; unchanged");
  else np_log("photo folder: %s failed (hr 0x%08lX); unchanged", step, (unsigned long)hr);
  if (chosen) taskFree(chosen);
  if (got) got->Release();
  if (start) start->Release();
  if (dlg) dlg->Release();
  if (coOn) coUninit();
  if (sh) FreeLibrary(sh);
  if (ole) FreeLibrary(ole);
}

void photo_folder_frame(void) {
  if (!s_request) return;
  s_request = false;
  pick();
}
