# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
  Builds the desktop tools (pocketchess-setup, PgnImporter, PuzzleImporter) for each installer
  platform with Zig, and with -Package the installer zips (ADR 0019):
    out/installer/PocketChess-setup-<platform>.zip
      pocketchess-setup[.exe]  tools/  reader/applications/ (the reader bundle)  reader/icons/  README.txt
  The reader bundle comes from projects/pocketbook/build-zig.ps1 (-Configuration Release) and leaves
  out content that may not be redistributed (historical.db: PGN Mentor); the tool fetches that.
.EXAMPLE
  projects/tools/build-tools.ps1 -Package
  projects/tools/build-tools.ps1 -Platforms linux-x64
#>
[CmdletBinding()]
param(
    [string[]]$Platforms = @('windows-x64', 'linux-x64', 'macos-arm64', 'macos-x64'),
    [switch]$Package,
    [string]$ToolchainRoot = ''
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$targets = @{
    'windows-x64' = 'x86_64-windows-gnu'
    'linux-x64'   = 'x86_64-linux-musl'
    'macos-arm64' = 'aarch64-macos'
    'macos-x64'   = 'x86_64-macos'
}
# Content that must not be redistributed (docs/licensing.md): fetched by the tool instead.
$notShipped = Join-Path $RepoRoot 'projects\not-redistributable.txt'

function Find-Tool([string]$name, [string[]]$candidates) {
    foreach ($c in $candidates) { if ($c -and (Test-Path $c)) { return (Resolve-Path $c).Path } }
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}
if (-not $ToolchainRoot) {
    $ToolchainRoot = if ($env:POCKETBOOK_TOOLCHAIN_ROOT) { $env:POCKETBOOK_TOOLCHAIN_ROOT } else { Join-Path $RepoRoot '..\..\libs\pocketbook' }
}
$zigDir = Join-Path $ToolchainRoot 'zig'
$zigCandidates = @()
if (Test-Path $zigDir) {
    $zigCandidates += @(Get-ChildItem -LiteralPath $zigDir -Directory |
        Sort-Object { try { [version]$_.Name } catch { [version]'0.0' } } -Descending |
        ForEach-Object { Join-Path $_.FullName 'zig.exe' })
}
$zig = Find-Tool 'zig' $zigCandidates
$vsRoots = @()
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) { $vsRoots += @(& $vswhere -all -prerelease -products * -sort -property installationPath) }
$vsTools = @($vsRoots | Where-Object { $_ } | ForEach-Object { Join-Path $_ 'Common7\IDE\CommonExtensions\Microsoft\CMake' })
$cmake = Find-Tool 'cmake' @($vsTools | ForEach-Object { Join-Path $_ 'CMake\bin\cmake.exe' })
$ninja = Find-Tool 'ninja' @($vsTools | ForEach-Object { Join-Path $_ 'Ninja\ninja.exe' })
if (-not ($zig -and $cmake -and $ninja)) { throw "zig, cmake and ninja are needed (zig in $zigDir\<version>\)" }

$env:ZIG_EXE = $zig
foreach ($p in $Platforms) {
    if (-not $targets.ContainsKey($p)) { throw "unknown platform $p ($($targets.Keys -join ', '))" }
    $env:ZIG_TARGET = $targets[$p]
    $build = Join-Path $RepoRoot "out\tools\$p"
    Write-Host "== tools for $p ($($env:ZIG_TARGET))"
    & $cmake -S $PSScriptRoot -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $PSScriptRoot 'zig-tool.cmake')" | Out-Null
    if ($LASTEXITCODE) { throw "cmake configure failed for $p" }
    & $cmake --build $build
    if ($LASTEXITCODE) { throw "build failed for $p" }
}

if ($Package) {
    $bundle = Join-Path $RepoRoot 'out\pocketbook-zig\Release\bundle\applications'
    if (-not (Test-Path (Join-Path $bundle 'pocketchess.app'))) {
        throw "No reader bundle in ${bundle}: run projects/pocketbook/build-zig.ps1 -Configuration Release first"
    }
    $outDir = Join-Path $RepoRoot 'out\installer'
    New-Item -ItemType Directory -Force $outDir | Out-Null
    foreach ($p in $Platforms) {
        $exe = if ($p -like 'windows*') { '.exe' } else { '' }
        $stage = Join-Path $outDir "stage-$p"
        if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
        New-Item -ItemType Directory -Force (Join-Path $stage 'tools'), (Join-Path $stage 'reader\icons') | Out-Null
        $build = Join-Path $RepoRoot "out\tools\$p"
        Copy-Item (Join-Path $build "pocketchess-setup$exe") $stage
        foreach ($t in 'PgnImporter', 'PuzzleImporter') { Copy-Item (Join-Path $build "$t$exe") (Join-Path $stage 'tools') }
        Copy-Item -Recurse $bundle (Join-Path $stage 'reader\applications')
        Copy-Item (Join-Path $RepoRoot 'data\icons\*.bmp') (Join-Path $stage 'reader\icons')
        Copy-Item (Join-Path $RepoRoot 'docs\installer.md') (Join-Path $stage 'README.txt')
        Copy-Item (Join-Path $RepoRoot 'LICENSE') (Join-Path $stage 'LICENSE.txt')
        Copy-Item (Join-Path $RepoRoot 'THIRD-PARTY-NOTICES.txt') $stage
        $zip = Join-Path $outDir "PocketChess-setup-$p.zip"
        & (Join-Path $RepoRoot 'scripts\zip-folder.ps1') -Folder $stage -Zip $zip -ExcludeFile $notShipped
        Remove-Item -Recurse -Force $stage
        Write-Host ("Package: {0} ({1:N0} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))
    }
}
exit 0
