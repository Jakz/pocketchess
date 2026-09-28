# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
  Copies the pinned SDL3 snapshot (include\, src\ and licence files only) from the Enigmistica
  repository into libs\sdl3 (build doc sec. 2). Nothing is downloaded.
  The committed projects\msvc\SDL3.vcxproj runs this automatically on its first build when
  libs\sdl3 is missing; run it by hand with -Force to refresh after updating Enigmistica's pin.
.EXAMPLE
  scripts\vendor-libs.ps1
  scripts\vendor-libs.ps1 -Enigmistica D:\src\enigmistica -Force
#>
[CmdletBinding()]
param(
    [string]$Enigmistica = '',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $Enigmistica) { $Enigmistica = Join-Path $RepoRoot '..\enigmistica' }
if (-not (Test-Path $Enigmistica)) {
    throw "Enigmistica not found at $Enigmistica. Pass -Enigmistica <path>, or copy SDL 3.4.16's include\ and src\ into libs\sdl3 by hand."
}
$Enigmistica = (Resolve-Path $Enigmistica).Path

$srcSdl = Join-Path $Enigmistica 'libs\sdl3'
$dstSdl = Join-Path $RepoRoot 'libs\sdl3'
if (-not (Test-Path (Join-Path $srcSdl 'include\SDL3\SDL.h'))) { throw "No SDL3 snapshot at $srcSdl" }

if ((Test-Path (Join-Path $dstSdl 'include\SDL3\SDL.h')) -and -not $Force) {
    Write-Host 'libs\sdl3 already present (use -Force to refresh)'
    exit 0
}

Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $dstSdl
New-Item -ItemType Directory -Force -Path $dstSdl | Out-Null
foreach ($dir in 'include', 'src') {
    Write-Host "Copying $srcSdl\$dir -> $dstSdl\$dir"
    robocopy (Join-Path $srcSdl $dir) (Join-Path $dstSdl $dir) /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE) for $dir" }
}
foreach ($file in 'LICENSE.txt', 'README.md', 'CREDITS.md') {
    $f = Join-Path $srcSdl $file
    if (Test-Path $f) { Copy-Item -LiteralPath $f -Destination $dstSdl -Force }
}
Set-Content -LiteralPath (Join-Path $dstSdl 'VENDORED.txt') -Encoding UTF8 -Value @(
    'SDL 3.4.16, commit fa2c02bb6e21974a89ea9824bc53c9932abe5f9c (release-3.4.16).'
    "Copied from $srcSdl by scripts\vendor-libs.ps1 on $(Get-Date -Format 'yyyy-MM-dd')."
    'Only include\, src\ and the licence files are kept; projects\msvc\SDL3.vcxproj builds them.'
)
Write-Host 'SDL3 vendored into libs\sdl3.'
# robocopy leaves a non-zero code for "files copied"; report success explicitly to MSBuild.
exit 0
