# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
  Builds engine plug-ins (D8, build doc sec. 4.3) with Zig for the device (arm) or Windows (windows-x64).
  Sources live in libs/<id>/ (local only, scripts/fetch-engines.ps1). Output: out/engines/<id>/<target>/.
.EXAMPLE
  projects/pocketbook/build-engine.ps1 -Target windows-x64
  projects/pocketbook/build-engine.ps1 -Engine stockfish -Target arm
#>
[CmdletBinding()]
param(
    [string]$Engine = '',
    [ValidateSet('arm', 'windows-x64')][string]$Target = 'arm',
    [string]$ToolchainRoot = '',
    [string]$ZigPath = '',
    [switch]$AllowMissing,   # warn instead of failing when libs/<id>/ is missing (VS Engines project)
    [switch]$Clean,
    [switch]$NoSmokeTest
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

function Find-Tool([string]$name, [string[]]$candidates) {
    foreach ($c in $candidates) { if ($c -and (Test-Path $c)) { return (Resolve-Path $c).Path } }
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

if (-not $ToolchainRoot) {
    if ($env:POCKETBOOK_TOOLCHAIN_ROOT) { $ToolchainRoot = $env:POCKETBOOK_TOOLCHAIN_ROOT }
    else { $ToolchainRoot = Join-Path $RepoRoot '..\..\libs\pocketbook' }
}
$ids = if ($Engine) { @($Engine) } else {
    @(Get-Content (Join-Path $RepoRoot 'projects\engines.txt') | ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') })
}

# zig.exe: -ZigPath, then the toolchain (zig\<version>\zig.exe, newest first), then PATH.
$zigCandidates = @($ZigPath, (Join-Path $ToolchainRoot 'zig\zig.exe'), (Join-Path $ToolchainRoot 'zig.exe'))
$zigDir = Join-Path $ToolchainRoot 'zig'
if (Test-Path $zigDir) {
    $zigCandidates += @(Get-ChildItem -LiteralPath $zigDir -Directory |
        Sort-Object { try { [version]$_.Name } catch { [version]'0.0' } } -Descending |
        ForEach-Object { Join-Path $_.FullName 'zig.exe' })
}
$zig = Find-Tool 'zig' $zigCandidates
# cmake + ninja: any Visual Studio install with the "C++ CMake tools" component, then PATH.
$vsRoots = @()
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) { $vsRoots += @(& $vswhere -all -prerelease -products * -sort -property installationPath) }
$vsRoots += 'C:\Program Files\Microsoft Visual Studio\18\Community'
$vsTools = @($vsRoots | Where-Object { $_ } | ForEach-Object { Join-Path $_ 'Common7\IDE\CommonExtensions\Microsoft\CMake' })
$cmake = Find-Tool 'cmake' @($vsTools | ForEach-Object { Join-Path $_ 'CMake\bin\cmake.exe' })
$ninja = Find-Tool 'ninja' @($vsTools | ForEach-Object { Join-Path $_ 'Ninja\ninja.exe' })

foreach ($id in $ids) {
    $src = Join-Path $RepoRoot "libs\$id"
    $proj = Join-Path $PSScriptRoot "engines\$id"
    if (-not (Test-Path (Join-Path $src 'src'))) {
        $msg = "Engine '$id': sources missing in libs\$id\ (not in Git). Run scripts\fetch-engines.ps1 -Engine $id"
        if ($AllowMissing) { Write-Warning $msg; continue }
        throw $msg
    }
    $missing = @(@{zig = $zig; cmake = $cmake; ninja = $ninja}.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object { $_.Key })
    if ($missing) {
        throw "Not found: $($missing -join ', '). zig is looked for in $ToolchainRoot\zig\<version>\ (or -ZigPath); cmake/ninja in the Visual Studio 'C++ CMake tools' component or on PATH."
    }
    Write-Host "zig: $zig`ncmake: $cmake`nninja: $ninja"

    $toolchainFile = if ($Target -eq 'arm') { Join-Path $ToolchainRoot 'ZigToolchain.cmake' } else { Join-Path $PSScriptRoot 'engines\zig-windows-x64.cmake' }
    $buildDir = Join-Path $RepoRoot "out\engines-build\$id\$Target"
    $outDir = Join-Path $RepoRoot "out\engines\$id\$Target"
    if ($Clean) { Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $buildDir, $outDir }
    $env:ZIG_EXE = $zig
    $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $RepoRoot 'out\pocketbook-zig\zig-global-cache'
    $env:ZIG_LOCAL_CACHE_DIR = Join-Path $RepoRoot 'out\pocketbook-zig\zig-local-cache'

    Write-Host "== Engine $id ($Target)"
    & $cmake -S $proj -B $buildDir -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile" "-DPOCKETBOOK_TOOLCHAIN_ROOT=$ToolchainRoot" "-DZIG_EXE=$zig" `
        "-DENGINE_SOURCE_DIR=$src" "-DPCHESS_ENGINE_TARGET=$Target"
    if ($LASTEXITCODE) { throw "configure failed for $id" }
    & $cmake --build $buildDir
    if ($LASTEXITCODE) { throw "build failed for $id" }

    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $bin = if ($Target -eq 'windows-x64') { "$id.exe" } else { $id }
    Copy-Item (Join-Path $buildDir $bin) $outDir -Force
    # Networks shipped beside the binary when not embedded (R2).
    $ship = Join-Path $buildDir 'ship'
    Get-ChildItem -Path $outDir -Filter *.nnue -ErrorAction SilentlyContinue | Remove-Item -Force
    if (Test-Path $ship) { Get-ChildItem -LiteralPath $ship -File | Copy-Item -Destination $outDir -Force }
    Write-Host "Engine $id -> $outDir"

    if ($Target -eq 'windows-x64' -and -not $NoSmokeTest -and $id -like 'stockfish*') {
        # Stockfish prints the bench summary on stderr; Windows PowerShell 5.1 turns redirected
        # stderr into errors under 'Stop', so let cmd merge the streams.
        $exePath = Join-Path $outDir $bin
        $out = cmd /c "`"$exePath`" bench 16 1 8 2>&1" | Out-String
        if ($out -notmatch 'Nodes searched\s*:\s*(\d+)') { throw "Smoke test failed for $id`n$out" }
        Write-Host "Smoke test: bench -> $($Matches[1]) nodes"
    }
}
exit 0
