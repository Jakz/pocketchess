# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
  One-time M0.2 helper: copies the pinned SDL3 snapshot and its static-library project from the
  Enigmistica repository (build doc sec. 2: "copy the pinned snapshot already in Enigmistica"),
  adapting the project to this repo (relative paths, fixed ProjectGuid used by PocketChess.sln).
  Nothing is downloaded. Re-run with -Force to refresh after updating Enigmistica's pin.
.EXAMPLE
  scripts/vendor-libs.ps1
  scripts/vendor-libs.ps1 -Enigmistica D:\src\enigmistica -Force
#>
[CmdletBinding()]
param(
    [string]$Enigmistica = '',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $Enigmistica) { $Enigmistica = Join-Path $RepoRoot '..\enigmistica' }
if (-not (Test-Path $Enigmistica)) { throw "Enigmistica not found at $Enigmistica (pass -Enigmistica)" }
$Enigmistica = (Resolve-Path $Enigmistica).Path
$SdlGuid = '{6E2B7C41-3F7A-4C1E-9A11-0C5D2B7A1009}'  # referenced by PocketChess.sln / PocketChess.vcxproj

# 1. SDL3 sources.
$srcSdl = Join-Path $Enigmistica 'libs\sdl3'
$dstSdl = Join-Path $RepoRoot 'libs\sdl3'
if (-not (Test-Path $srcSdl)) { throw "No SDL3 snapshot at $srcSdl" }
if ((Test-Path $dstSdl) -and -not $Force) {
    Write-Host 'libs\sdl3 already present (use -Force to refresh)'
} else {
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $dstSdl
    Write-Host "Copying $srcSdl -> $dstSdl"
    robocopy $srcSdl $dstSdl /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }
}

# 2. SDL3.vcxproj (+ filters): find it in Enigmistica, rewrite paths for projects\msvc\.
$proj = Get-ChildItem -Path $Enigmistica -Recurse -Filter 'SDL3.vcxproj' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch '\\libs\\' -and $_.FullName -notmatch '\\out\\' } | Select-Object -First 1
if (-not $proj) { throw 'SDL3.vcxproj not found in Enigmistica (expected projects\importer\msvc\SDL3.vcxproj)' }
Write-Host "Adapting $($proj.FullName)"
$srcDir = Split-Path -Parent $proj.FullName
$dstDir = Join-Path $RepoRoot 'projects\msvc'
foreach ($name in 'SDL3.vcxproj', 'SDL3.vcxproj.filters') {
    $src = Join-Path $srcDir $name
    if (-not (Test-Path $src)) { continue }
    $text = Get-Content -LiteralPath $src -Raw
    # Rebase every relative path that points into Enigmistica's libs\sdl3 onto ours.
    $text = [regex]::Replace($text, '(\.\.\\)+libs\\sdl3', '..\..\libs\sdl3')
    $text = $text.Replace('$(ProjectDir)..\..\..\libs\sdl3', '$(ProjectDir)..\..\libs\sdl3')
    if ($name -eq 'SDL3.vcxproj') {
        $text = [regex]::Replace($text, '<ProjectGuid>\{[0-9A-Fa-f-]+\}</ProjectGuid>', "<ProjectGuid>$SdlGuid</ProjectGuid>")
        # Keep SDL's own compiler settings (our /W4 /sdl flags are for first-party code only).
        # Enigmistica's per-project output folders are fine: the ProjectReference finds the .lib.
    }
    [IO.File]::WriteAllText((Join-Path $dstDir $name), $text)
}

# 3. Sanity: every ClCompile and every imported .props of SDL3.vcxproj must exist.
[xml]$xml = Get-Content -LiteralPath (Join-Path $dstDir 'SDL3.vcxproj') -Raw
$missing = @()
foreach ($imp in $xml.GetElementsByTagName('Import')) {
    $p = $imp.GetAttribute('Project')
    if ($p -and $p -notmatch '\$\(' -and -not (Test-Path (Join-Path $dstDir $p))) { $missing += $p }
}
foreach ($g in $xml.Project.ItemGroup) {
    foreach ($c in @($g.ClCompile)) {
        if ($c -and $c.Include -and -not (Test-Path (Join-Path $dstDir $c.Include))) { $missing += $c.Include }
    }
}
if ($missing) {
    Write-Warning "SDL3.vcxproj references $($missing.Count) missing file(s), e.g. $($missing[0]). Fix the paths by hand."
    exit 1
}
Write-Host 'SDL3 vendored. Record the version in libs\README.md (copy the row from Enigmistica).'
exit 0
