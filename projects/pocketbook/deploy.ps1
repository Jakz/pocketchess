# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
  Copies a PocketBook build to the reader over USB (build doc sec. 4.4):
  pocketchess.app (+ the self-test) to \applications\ and the bundle to \applications\pocketchess\
  WITHOUT touching user\ or logs\ (progress is never overwritten, design notes sec. 24.1).
.EXAMPLE
  projects/pocketbook/deploy.ps1 -Configuration Release -Eject
#>
[CmdletBinding()]
param(
    [string]$Drive = '',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$WithContent,
    # Extra puzzle packs from data/generated/puzzle-packs (build-content.ps1 -PuzzlePacks), e.g. 500k.
    # They go to user\content\puzzles\ like an Import, so later deploys never remove them.
    [string[]]$Puzzles = @(),
    [switch]$NoSelfTest,
    [switch]$Eject
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$bundle = Join-Path $RepoRoot "out\pocketbook-zig\$Configuration\bundle\applications"
if (-not (Test-Path $bundle)) { throw "No bundle at $bundle. Run projects/pocketbook/build-zig.ps1 -Configuration $Configuration first." }

if (-not $Drive) {
    $candidates = Get-PSDrive -PSProvider FileSystem | Where-Object { $_.Root -and (Test-Path (Join-Path $_.Root 'applications')) }
    if (-not $candidates) { throw 'No reader found (a drive with an \applications folder). Connect it over USB or pass -Drive E:' }
    $Drive = $candidates[0].Root
}
$Drive = $Drive.TrimEnd('\') + '\'
$apps = Join-Path $Drive 'applications'
Write-Host "Deploying $Configuration to $apps"

Copy-Item (Join-Path $bundle 'pocketchess.app') $apps -Force
if (-not $NoSelfTest) { Copy-Item (Join-Path $bundle 'pocketchess-selftest.app') $apps -Force }

$src = Join-Path $bundle 'pocketchess'
$dst = Join-Path $apps 'pocketchess'
$exclude = @('user', 'logs', 'imports', 'exports')
if (-not $WithContent) { $exclude += 'content' }
# /MIR inside the bundle folders only; excluded folders on the device are never touched.
robocopy $src $dst /MIR /XD @exclude /NFL /NDL /NJH /NP | Out-Host
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }
foreach ($d in 'user', 'logs', 'imports', 'exports') { New-Item -ItemType Directory -Force -Path (Join-Path $dst $d) | Out-Null }
foreach ($size in $Puzzles) {
    $pack = Join-Path $RepoRoot "data\generated\puzzle-packs\puzzles-$size.db"
    if (-not (Test-Path $pack)) { throw "No puzzle pack $pack (scripts/build-content.ps1 -PuzzlePacks)" }
    $target = Join-Path $dst 'user\content\puzzles'
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Write-Host ("Puzzle pack {0} ({1:N0} MB) -> {2}" -f $size, ((Get-Item $pack).Length / 1MB), $target)
    Copy-Item -Force $pack $target
}

# Sizes against the budget (architecture doc sec. 13).
$size = (Get-ChildItem -Recurse -File $src | Measure-Object -Property Length -Sum).Sum + (Get-Item (Join-Path $bundle 'pocketchess.app')).Length
Write-Host ("Bundle size: {0:N1} MB (budget 150 MB)" -f ($size / 1MB))
Get-ChildItem -Directory $src | ForEach-Object {
    $s = (Get-ChildItem -Recurse -File $_.FullName | Measure-Object -Property Length -Sum).Sum
    Write-Host ("  {0,-10} {1,8:N1} MB" -f $_.Name, ($s / 1MB))
}

if ($Eject) {
    $shell = New-Object -ComObject Shell.Application
    $shell.Namespace(17).ParseName($Drive.TrimEnd('\')).InvokeVerb('Eject')
    Write-Host "Ejected $Drive"
}
exit 0
