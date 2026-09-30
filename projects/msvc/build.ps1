# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
  Command-line/CI build of PocketChess.sln (build doc sec. 3.3). Not needed inside Visual Studio.
.EXAMPLE
  projects/msvc/build.ps1 -Configuration Desktop -Target Test
  projects/msvc/build.ps1 -Configuration Release -Package
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'Desktop')][string]$Configuration = 'Desktop',
    [ValidateSet('Build', 'Rebuild', 'Clean', 'Test')][string]$Target = 'Build',
    [switch]$Package
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found: install Visual Studio 2026 with the C++ workload' }
$msbuild = & $vswhere -latest -prerelease -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild not found' }

$sln = Join-Path $PSScriptRoot 'PocketChess.sln'
$msTarget = if ($Target -eq 'Test') { 'Build' } else { $Target }
& $msbuild $sln -m -nologo -v:minimal "-t:$msTarget" "-p:Configuration=$Configuration" '-p:Platform=x64' '-p:RunTestsAfterBuild=false'
if ($LASTEXITCODE) { throw "MSBuild failed ($LASTEXITCODE)" }

$outDir = Join-Path $RepoRoot "out\msvc\$Configuration"
if ($Target -eq 'Test' -or $Target -eq 'Build') {
    Push-Location $RepoRoot
    try {
        & (Join-Path $outDir 'PocketChessTests.exe')
        if ($LASTEXITCODE) { throw "Tests failed ($LASTEXITCODE)" }
    } finally { Pop-Location }
}

if ($Package) {
    $zip = Join-Path $RepoRoot "out\PocketChess-windows-$Configuration.zip"
    $stage = Join-Path $RepoRoot "out\package-windows"
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $stage, $zip
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    Copy-Item (Join-Path $outDir 'PocketChess.exe'), (Join-Path $outDir 'SDL3.dll') $stage
    Copy-Item -Recurse (Join-Path $outDir 'pocketchess') $stage
    # Nothing from a development run: user data, logs, imports and exports start empty (AGENTS.md).
    foreach ($d in 'user', 'logs', 'imports', 'exports') {
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $stage "pocketchess\$d")
        New-Item -ItemType Directory -Force -Path (Join-Path $stage "pocketchess\$d") | Out-Null
    }
    & (Join-Path $RepoRoot 'scripts\zip-folder.ps1') -Folder $stage -Zip $zip
    Write-Host "Package: $zip"
}
exit 0
