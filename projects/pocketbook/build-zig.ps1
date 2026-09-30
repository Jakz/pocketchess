# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
  PocketBook build (build doc sec. 4.2): CMake + Ninja + the shared Zig toolchain.
  Produces out/pocketbook-zig/<cfg>/pocketchess.app, pocketchess-selftest.app and the staged
  bundle out/pocketbook-zig/<cfg>/bundle/ (applications/pocketchess.app + applications/pocketchess/).
.EXAMPLE
  projects/pocketbook/build-zig.ps1 -Configuration Release
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'Desktop')][string]$Configuration = 'Release',
    [string]$ToolchainRoot = '',
    [string]$SdkLocal = '',        # override the InkView SDK folder (default <toolchain>/inkview/6.10)
    [string]$ZigPath = '',
    [switch]$SkipEngines,
    [string]$Content = '',
    [switch]$Clean,
    [switch]$Package               # also zip the bundle (M6 release)
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ($Configuration -eq 'Desktop') { $Configuration = 'Debug' }  # VS 'Desktop' never calls this; be tolerant

function Resolve-Toolchain {
    if ($ToolchainRoot) { return (Resolve-Path $ToolchainRoot).Path }
    if ($env:POCKETBOOK_TOOLCHAIN_ROOT) { return (Resolve-Path $env:POCKETBOOK_TOOLCHAIN_ROOT).Path }
    $rel = Join-Path $RepoRoot '..\..\libs\pocketbook'  # same relative path as Enigmistica (D3)
    if (Test-Path $rel) { return (Resolve-Path $rel).Path }
    throw "PocketBook toolchain not found. Pass -ToolchainRoot, set POCKETBOOK_TOOLCHAIN_ROOT, or place it at $rel"
}

function Find-Tool([string]$name, [string[]]$candidates) {
    foreach ($c in $candidates) { if ($c -and (Test-Path $c)) { return (Resolve-Path $c).Path } }
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

# Every Visual Studio install (newest first): the "C++ CMake tools" component ships cmake + ninja.
function Get-VsInstallPaths {
    $paths = @()
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) { $paths += @(& $vswhere -all -prerelease -products * -sort -property installationPath) }
    $paths += 'C:\Program Files\Microsoft Visual Studio\18\Community'  # Enigmistica's fallback
    return $paths | Where-Object { $_ }
}

# zig.exe: -ZigPath, then the toolchain (zig\<version>\zig.exe, newest first), then PATH.
function Get-ZigCandidates([string]$toolchain, [string]$explicit) {
    $c = @($explicit, (Join-Path $toolchain 'zig\zig.exe'), (Join-Path $toolchain 'zig.exe'))
    $zigDir = Join-Path $toolchain 'zig'
    if (Test-Path $zigDir) {
        $c += @(Get-ChildItem -LiteralPath $zigDir -Directory | Sort-Object { try { [version]$_.Name } catch { [version]'0.0' } } -Descending |
                ForEach-Object { Join-Path $_.FullName 'zig.exe' })
    }
    return $c
}

$toolchain = Resolve-Toolchain
$toolchainFile = Join-Path $toolchain 'ZigToolchain.cmake'
if (-not (Test-Path $toolchainFile)) { throw "Missing $toolchainFile" }
$sdk = if ($SdkLocal) { (Resolve-Path $SdkLocal).Path } else { Join-Path $toolchain 'inkview\6.10' }

$zig = Find-Tool 'zig' (Get-ZigCandidates $toolchain $ZigPath)
if (-not $zig) { throw "zig not found in $toolchain\zig\<version>\ (pass -ZigPath or put zig on PATH)" }
$vsTools = @(Get-VsInstallPaths | ForEach-Object { Join-Path $_ 'Common7\IDE\CommonExtensions\Microsoft\CMake' })
$cmake = Find-Tool 'cmake' @($vsTools | ForEach-Object { Join-Path $_ 'CMake\bin\cmake.exe' })
$ninja = Find-Tool 'ninja' @($vsTools | ForEach-Object { Join-Path $_ 'Ninja\ninja.exe' })
if (-not $cmake -or -not $ninja) { throw 'cmake/ninja not found (install the VS "C++ CMake tools" component or put them on PATH)' }

$outRoot = Join-Path $RepoRoot 'out\pocketbook-zig'
$buildDir = Join-Path $outRoot $Configuration
$env:ZIG_EXE = $zig
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $outRoot 'zig-global-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $outRoot 'zig-local-cache'
if ($Clean -and (Test-Path $buildDir)) { Remove-Item -Recurse -Force $buildDir }

Write-Host "== PocketBook $Configuration  (zig: $zig)"
$configureArgs = @(
    '-S', (Join-Path $RepoRoot 'projects\pocketbook'),
    '-B', $buildDir,
    '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$ninja",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile",
    "-DPOCKETBOOK_TOOLCHAIN_ROOT=$toolchain",
    "-DPOCKETBOOK_SDK_ROOT=$sdk",
    "-DZIG_EXE=$zig"
)
& $cmake @configureArgs
if ($LASTEXITCODE) { throw "CMake configure failed ($LASTEXITCODE)" }
& $cmake --build $buildDir --target pchess-pocketbook pchess-selftest
if ($LASTEXITCODE) { throw "Build failed ($LASTEXITCODE)" }

$app = Join-Path $buildDir 'pocketchess.app'
$selftest = Join-Path $buildDir 'pocketchess-selftest.app'
& (Join-Path $PSScriptRoot 'check-elf.ps1') -Path $app, $selftest
if ($LASTEXITCODE) { throw 'ELF checks failed' }

if (-not $SkipEngines) {
    & (Join-Path $PSScriptRoot 'build-engine.ps1') -Target arm -ToolchainRoot $toolchain -ZigPath $zig -AllowMissing
    if ($LASTEXITCODE) { throw 'Engine build failed' }
}

# Bundle: <bundle>/applications/pocketchess.app + <bundle>/applications/pocketchess/
$bundle = Join-Path $buildDir 'bundle'
$apps = Join-Path $bundle 'applications'
New-Item -ItemType Directory -Force -Path $apps | Out-Null
Copy-Item $app, $selftest -Destination $apps -Force
& (Join-Path $RepoRoot 'scripts\stage-bundle.ps1') -Target pocketbook -Configuration $Configuration -Dest (Join-Path $apps 'pocketchess') -Content $Content
if ($LASTEXITCODE) { throw 'Staging failed' }

if ($Package) {
    $zip = Join-Path $outRoot "pocketchess-$Configuration.zip"
    if (Test-Path $zip) { Remove-Item $zip }
    & (Join-Path $RepoRoot 'scripts\zip-folder.ps1') -Folder $bundle -Zip $zip
    Write-Host "Package: $zip"
}
Write-Host "PocketBook build OK: $bundle"
exit 0
