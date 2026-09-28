# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
  Parity and layering checks (build doc sec. 6, R8):
  1. projects/pocketbook/sources.cmake lists == ClCompile items of PchessCore / PchessUi /
     PocketChessTests.vcxproj (TestMain.cpp is desktop-only).
  2. Every .cpp under src/core, src/ui, src/app and tests is listed.
  3. src/core, src/ui and src/app never include SDL, InkView, Windows or POSIX headers.
  Runs as a pre-build step of PchessCore and from scripts/verify-all.ps1.
#>
[CmdletBinding()]
param([string]$RepoRoot = '')

$ErrorActionPreference = 'Stop'
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path }
$errors = New-Object System.Collections.Generic.List[string]

function Get-CMakeList([string]$text, [string]$var) {
    $m = [regex]::Match($text, "set\(\s*$var\s+(.*?)\)", 'Singleline')
    if (-not $m.Success) { throw "sources.cmake: $var not found" }
    return @($m.Groups[1].Value -split "`r?`n" | ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') } |
             ForEach-Object { $_.Replace('${REPO_ROOT}/', '') })
}

function Get-VcxprojSources([string]$path) {
    [xml]$xml = Get-Content -LiteralPath $path -Raw
    $items = @()
    foreach ($group in $xml.Project.ItemGroup) {
        foreach ($c in @($group.ClCompile)) {
            if ($c -and $c.Include) { $items += ($c.Include -replace '^(\.\.\\)+', '').Replace('\', '/') }
        }
    }
    return $items
}

function Compare-Lists([string]$name, [string[]]$cmake, [string[]]$vcx) {
    foreach ($f in $cmake) { if ($vcx -notcontains $f) { $errors.Add("[$name] in sources.cmake but not in the .vcxproj: $f") } }
    foreach ($f in $vcx) { if ($cmake -notcontains $f) { $errors.Add("[$name] in the .vcxproj but not in sources.cmake: $f") } }
}

$cmakeText = Get-Content -LiteralPath (Join-Path $RepoRoot 'projects/pocketbook/sources.cmake') -Raw
$msvc = Join-Path $RepoRoot 'projects/msvc'
$core = Get-CMakeList $cmakeText 'PCHESS_CORE_SOURCES'
$ui = Get-CMakeList $cmakeText 'PCHESS_UI_SOURCES'
$tests = Get-CMakeList $cmakeText 'PCHESS_TEST_SOURCES'

Compare-Lists 'core' $core (Get-VcxprojSources (Join-Path $msvc 'PchessCore.vcxproj'))
Compare-Lists 'ui' $ui (Get-VcxprojSources (Join-Path $msvc 'PchessUi.vcxproj'))
Compare-Lists 'tests' $tests @(Get-VcxprojSources (Join-Path $msvc 'PocketChessTests.vcxproj') | Where-Object { $_ -ne 'tests/TestMain.cpp' })

# Every .cpp on disk must be listed somewhere.
function Get-RelativeCpp([string]$dir) {
    $abs = Join-Path $RepoRoot $dir
    if (-not (Test-Path $abs)) { return @() }
    return @(Get-ChildItem -LiteralPath $abs -Recurse -File -Filter *.cpp | ForEach-Object {
        $_.FullName.Substring($RepoRoot.TrimEnd('\', '/').Length + 1).Replace('\', '/') })
}
foreach ($f in (Get-RelativeCpp 'src/core')) { if ($core -notcontains $f) { $errors.Add("not listed (core): $f") } }
foreach ($f in (Get-RelativeCpp 'src/ui') + (Get-RelativeCpp 'src/app')) { if ($ui -notcontains $f) { $errors.Add("not listed (ui/app): $f") } }
foreach ($f in (Get-RelativeCpp 'tests')) {
    if ($f -ne 'tests/TestMain.cpp' -and $tests -notcontains $f) { $errors.Add("not listed (tests): $f") }
}

# Layering: no platform headers in portable code.
$forbidden = '^\s*#\s*include\s*[<"](SDL3/|SDL\.h|inkview\.h|windows\.h|unistd\.h|pthread\.h|sys/|psapi\.h|dlfcn\.h)'
foreach ($dir in 'src/core', 'src/ui', 'src/app') {
    $abs = Join-Path $RepoRoot $dir
    # Windows PowerShell 5.1 ignores -Include together with -LiteralPath (it returned folders too),
    # so filter by extension explicitly.
    Get-ChildItem -LiteralPath $abs -Recurse -File | Where-Object { $_.Extension -in '.h', '.cpp' } | ForEach-Object {
        $n = 0
        foreach ($line in Get-Content -LiteralPath $_.FullName) {
            $n++
            if ($line -match $forbidden) { $errors.Add("layering: $($_.Name):$n includes a platform header: $($line.Trim())") }
        }
    }
}

if ($errors.Count) {
    Write-Host "check-sources: $($errors.Count) problem(s)" -ForegroundColor Red
    $errors | ForEach-Object { Write-Host "  $_" }
    exit 1
}
Write-Host "check-sources: OK ($($core.Count) core, $($ui.Count) ui/app, $($tests.Count) test sources)"
exit 0
