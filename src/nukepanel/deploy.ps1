# Install build/winmm.dll and assets/nukepanel/* into the Enhanced Edition,
# then hash-compare every deployed file. Prints MATCH only when all match:
# do not launch the game until MATCH has been read.
# -GameDir is the Enhanced Edition folder (the one holding prism3d.exe); the
# NUKEPANEL_GAME_DIR environment variable is the fallback. Nothing is guessed.
# -SetDxwrapperFps0 sets dxwrapper.ini's LimitPerFrameFPS to 0 (a byte-exact backup is kept); off by default.
# -SelfTest checks the folder, ownership and mirror rules on a temp dir and touches nothing else.
param([switch]$SelfTest, [string]$GameDir = '', [switch]$SetDxwrapperFps0)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Resolve-Path (Join-Path $root '../..')
$game = if ($GameDir) { $GameDir } else { $env:NUKEPANEL_GAME_DIR }

# A folder is the Enhanced Edition when it holds prism3d.exe and duke/base: a slip of -GameDir must not plant a
# winmm.dll in some other folder.
function Test-GameFolder($dir) {
  return (Test-Path -LiteralPath (Join-Path $dir 'prism3d.exe') -PathType Leaf) -and
         (Test-Path -LiteralPath (Join-Path $dir 'duke/base') -PathType Container)
}

# A file is ours to overwrite when absent, when its hash is the one we recorded writing, or when it already holds the
# bytes we would write (the copy changes nothing). Anything else, such as another mod's winmm.dll, is refused.
function Test-Owned($dst, $recordedHash, $srcHash) {
  if (-not (Test-Path -LiteralPath $dst)) { return $true }
  $now = (Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash
  return ($recordedHash -and $now -eq $recordedHash) -or ($srcHash -and $now -eq $srcHash)
}

# Files under $dstDir with no counterpart under $srcDir: the mounted folders sit above the game's data, so a stale
# file there would shadow a game file for as long as winmm.dll is installed.
function Get-StaleFiles($dstDir, $srcDir) {
  if (-not (Test-Path -LiteralPath $dstDir -PathType Container)) { return @() }
  $dstFull = (Resolve-Path -LiteralPath $dstDir).Path
  return @(Get-ChildItem -LiteralPath $dstFull -File -Recurse | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $srcDir $_.FullName.Substring($dstFull.Length + 1)) -PathType Leaf)
  } | ForEach-Object { $_.FullName })
}

if ($SelfTest) {
  $t = Join-Path ([IO.Path]::GetTempPath()) ("np-deploy-selftest-" + [guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Force -Path $t | Out-Null
  try {
    $cases = @( @{ n = 'not-a-game-dir'; ok = (Test-GameFolder $t) -eq $false } )
    [IO.File]::WriteAllText((Join-Path $t 'prism3d.exe'), 'x')
    $cases += @{ n = 'exe-without-base'; ok = (Test-GameFolder $t) -eq $false }
    New-Item -ItemType Directory -Force -Path (Join-Path $t 'duke/base') | Out-Null
    $cases += @{ n = 'game-dir'; ok = (Test-GameFolder $t) -eq $true }
    $cases += @{ n = 'missing-dir'; ok = (Test-GameFolder (Join-Path $t 'nope')) -eq $false }

    $ours = Join-Path $t 'ours.bin'; [IO.File]::WriteAllText($ours, 'ours')
    $foreign = Join-Path $t 'foreign.bin'; [IO.File]::WriteAllText($foreign, 'foreign')
    $changed = Join-Path $t 'changed.bin'; [IO.File]::WriteAllText($changed, 'edited later')
    $oursHash = (Get-FileHash -LiteralPath $ours -Algorithm SHA256).Hash
    $newHash = 'AB' * 32
    $cases += @{ n = 'absent';           ok = (Test-Owned (Join-Path $t 'nope.bin') $null $newHash) -eq $true }
    $cases += @{ n = 'ours';             ok = (Test-Owned $ours $oursHash $newHash) -eq $true }
    $cases += @{ n = 'foreign';          ok = (Test-Owned $foreign $null $newHash) -eq $false }
    $cases += @{ n = 'recorded-changed'; ok = (Test-Owned $changed $oursHash $newHash) -eq $false }
    $cases += @{ n = 'same-as-source';   ok = (Test-Owned $ours $null $oursHash) -eq $true }

    $src = Join-Path $t 'src/aspect43'; $dst = Join-Path $t 'dst/aspect43'
    foreach ($f in @('ui/scripts/a.sc', 'b.sc')) {
      foreach ($d in @($src, $dst)) {
        $p = Join-Path $d $f
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $p) | Out-Null
        [IO.File]::WriteAllText($p, 'x')
      }
    }
    [IO.File]::WriteAllText((Join-Path $dst 'ui/scripts/stale.sc'), 'x')
    [IO.File]::WriteAllText((Join-Path $dst 'old.txt'), 'x')
    $stale = @(Get-StaleFiles $dst $src | ForEach-Object { $_.Substring($dst.Length + 1).Replace([string][char]92, '/') } |
               Sort-Object)
    $cases += @{ n = 'mirror-stale'; ok = ($stale -join ',') -eq 'old.txt,ui/scripts/stale.sc' }
    $cases += @{ n = 'mirror-no-dst'; ok = @(Get-StaleFiles (Join-Path $t 'nope') $src).Count -eq 0 }

    $pass = 0; $fail = 0
    foreach ($c in $cases) { if ($c.ok) { $pass++ } else { $fail++; "FAIL $($c.n)" } }
    "deploy self-test: $pass passed, $fail failed"
    if ($fail -gt 0) { exit 1 }
    exit 0
  } finally { Remove-Item -LiteralPath $t -Recurse -Force -ErrorAction SilentlyContinue }
}

# The game folder must be named and exist, and be the Enhanced Edition.
if (-not $game -or -not (Test-Path -LiteralPath $game -PathType Container)) {
  Write-Host "ERROR: the Enhanced Edition folder was not given or does not exist: '$game'."
  Write-Host 'Pass -GameDir <folder> or set the NUKEPANEL_GAME_DIR environment variable.'
  exit 2
}
$game = (Resolve-Path -LiteralPath $game).Path
if (-not (Test-GameFolder $game)) {
  Write-Host "REFUSED: '$game' is not the Enhanced Edition folder (no prism3d.exe and duke/base there)."
  exit 2
}

# Never deploy under a running game: the files are in use.
if (Get-Process -Name prism3d -ErrorAction SilentlyContinue) {
  Write-Host 'REFUSED: prism3d.exe is running; close the game and deploy again'
  exit 2
}

# The only paths this script may write: GAME\winmm.dll, anything under GAME\duke\base\nukepanel\, and (with
# -SetDxwrapperFps0) dxwrapper.ini and its backup.
$assetDir = [IO.Path]::GetFullPath((Join-Path $game 'duke/base/nukepanel'))
function Assert-Target($path) {
  $full = [IO.Path]::GetFullPath($path)
  if ($full -ieq [IO.Path]::GetFullPath((Join-Path $game 'winmm.dll'))) { return }
  if ($full.StartsWith($assetDir + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { return }
  if ($SetDxwrapperFps0 -and ($full -ieq [IO.Path]::GetFullPath((Join-Path $game 'dxwrapper.ini')) -or
                              $full -ieq [IO.Path]::GetFullPath((Join-Path $game 'dxwrapper.ini.nukepanel-bak')))) { return }
  throw "refusing to write outside winmm.dll and duke/base/nukepanel/: $path"
}

# The game may hold the DLL open: retry while the file is locked.
function Copy-Retry($src, $dst) {
  Assert-Target $dst
  $deadline = (Get-Date).AddSeconds(30)
  while ($true) {
    try { Copy-Item -LiteralPath $src -Destination $dst -Force; return }
    catch [System.IO.IOException] {
      if ((Get-Date) -gt $deadline) { throw "still locked after 30 s: $dst" }
      Start-Sleep -Milliseconds 500
    }
  }
}

# pairs: src, dst, rel (path relative to the game folder, the key in deployed.txt)
$pairs = @(@{ src = (Join-Path $repo 'build/winmm.dll'); dst = (Join-Path $game 'winmm.dll'); rel = 'winmm.dll' })
# Recursive: aspect43/ui/scripts/ holds the 4:3 floating-text script our DLL mounts above
# db_wide (floattext.cpp); skipintro/ui/scripts/ the logo-less intro (skipintro.cpp). Inert without
# winmm.dll: nothing else mounts those folders.
$assetRoot = (Resolve-Path (Join-Path $repo 'assets/nukepanel')).Path
Get-ChildItem -File -Recurse $assetRoot | ForEach-Object {
  $rel = $_.FullName.Substring($assetRoot.Length + 1).Replace([string][char]92, '/')   # backslash, spelled so no tool eats it
  $pairs += @{ src = $_.FullName; dst = (Join-Path $assetDir $rel); rel = "duke/base/nukepanel/$rel" }
}

# What we last wrote: "sha256  relative-path" per line.
$record = Join-Path $assetDir 'deployed.txt'
$recorded = @{}
if (Test-Path -LiteralPath $record) {
  foreach ($line in Get-Content -LiteralPath $record) {
    if ($line -match '^([0-9A-Fa-f]{64})\s\s(.+)$') { $recorded[$Matches[2]] = $Matches[1].ToUpper() }
  }
}

# Every pair is checked before the first copy: one file we did not write stops the whole deploy.
foreach ($p in $pairs) {
  Assert-Target $p.dst
  if (-not (Test-Path -LiteralPath $p.src)) { throw "missing source $($p.src)" }
  $p.hash = (Get-FileHash -LiteralPath $p.src -Algorithm SHA256).Hash
  if (-not (Test-Owned $p.dst $recorded[$p.rel] $p.hash)) {
    Write-Host "REFUSED: $($p.dst) was not written by this script (another mod's file, or one edited since)."
    Write-Host 'Nothing was copied. Move that file away yourself if it should be replaced, then deploy again.'
    exit 3
  }
}

Assert-Target $record
New-Item -ItemType Directory -Force -Path $assetDir | Out-Null
# Each file is recorded right after its own copy, and earlier entries are kept: a deploy that fails midway never
# leaves one of our files looking foreign to the next deploy.
function Save-Record {
  $lines = @($recorded.Keys | Sort-Object | ForEach-Object { "$($recorded[$_])  $_" })
  [IO.File]::WriteAllText($record, (($lines -join "`n") + "`n"))
}
foreach ($p in $pairs) {
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $p.dst) | Out-Null
  Copy-Retry $p.src $p.dst
  $recorded[$p.rel] = (Get-FileHash -LiteralPath $p.dst -Algorithm SHA256).Hash
  Save-Record
}

# The two mounted folders mirror the repo: a file left from an earlier build, or dropped there by hand, would shadow
# the game's own file of that name.
foreach ($m in @('aspect43', 'skipintro')) {
  foreach ($f in Get-StaleFiles (Join-Path $assetDir $m) (Join-Path $assetRoot $m)) {
    Assert-Target $f
    Remove-Item -LiteralPath $f -Force
    $rel = 'duke/base/nukepanel/' + $f.Substring($assetDir.Length + 1).Replace([string][char]92, '/')
    if ($recorded.ContainsKey($rel)) { $recorded.Remove($rel); Save-Record }
    "  removed stale $f"
  }
}

$bad = 0
foreach ($p in $pairs) {
  $b = (Get-FileHash -LiteralPath $p.dst -Algorithm SHA256).Hash
  if ($p.hash -ne $b) { "MISMATCH $($p.dst)  src $($p.hash)  dst $b"; $bad++ }
  else { "  ok $($p.dst)  $b" }
}

# dxwrapper's own frame limiter throttles the gl path too (render.md's "Frame cap SETTLED"), so the panel's cap fights
# it unless it is 0. It is another program's file: edited only with -SetDxwrapperFps0, else a hint.
$dxIni = Join-Path $game 'dxwrapper.ini'
$dxBak = "$dxIni.nukepanel-bak"
if (-not (Test-Path -LiteralPath $dxIni)) {
  "dxwrapper: not found, nothing to check"
} else {
  # ISO-8859-1 maps every byte 0-255 to one char and back losslessly, so a round trip through .NET strings touches
  # no byte this script does not mean to change: every other byte and the line endings are preserved.
  $latin1 = [System.Text.Encoding]::GetEncoding(28591)
  $text = $latin1.GetString([System.IO.File]::ReadAllBytes($dxIni))
  $m = [regex]::Match($text, '(?m)^LimitPerFrameFPS\s*=\s*(\d+)')
  if (-not $m.Success) {
    "dxwrapper: LimitPerFrameFPS not found in $dxIni"
  } elseif ($m.Groups[1].Value -eq '0') {
    "dxwrapper: LimitPerFrameFPS 0 (already 0)"
  } elseif (-not $SetDxwrapperFps0) {
    "dxwrapper: LimitPerFrameFPS is $($m.Groups[1].Value); set it to 0 in dxwrapper.ini for the panel's frame cap, or deploy with -SetDxwrapperFps0"
  } else {
    Assert-Target $dxIni
    Assert-Target $dxBak
    if (-not (Test-Path -LiteralPath $dxBak)) { Copy-Item -LiteralPath $dxIni -Destination $dxBak -Force }
    $g = $m.Groups[1]
    $newText = $text.Substring(0, $g.Index) + '0' + $text.Substring($g.Index + $g.Length)
    [System.IO.File]::WriteAllBytes($dxIni, $latin1.GetBytes($newText))
    "dxwrapper: LimitPerFrameFPS 0 (backup kept as $dxBak)"
  }
}

if ($bad -gt 0) { exit 1 }
"MATCH ($($pairs.Count) files)"
