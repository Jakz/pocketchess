# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
  Builds the content packs in data/generated/ from data/downloads/ (architecture doc sec. 12).
  Both build scripts stage data/generated/ into content/ when it exists. No network access: run
  scripts/fetch-lichess.ps1 first. Uses the PuzzleImporter built by the MSVC solution.
.EXAMPLE
  scripts/build-content.ps1                       # content/puzzles/large.db (50k puzzles)
  scripts/build-content.ps1 -Presets starter,large
#>
[CmdletBinding()]
param(
    [ValidateSet('starter', 'standard', 'large', 'huge')][string[]]$Presets = @('large'),
    [string]$Configuration = 'Desktop',
    [uint64]$Seed = 1
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$importer = Join-Path $RepoRoot "out\msvc\$Configuration\PuzzleImporter.exe"
if (-not (Test-Path $importer)) { throw "PuzzleImporter not built: $importer (projects/msvc/build.ps1 -Configuration $Configuration)" }
$source = Join-Path $RepoRoot 'data\downloads\lichess_db_puzzle.csv.zst'
if (-not (Test-Path $source)) { throw "No puzzle download: $source (scripts/fetch-lichess.ps1)" }

$out = Join-Path $RepoRoot 'data\generated\puzzles'
New-Item -ItemType Directory -Force -Path $out | Out-Null
foreach ($preset in $Presets) {
    $db = Join-Path $out "$preset.db"
    Write-Host "== puzzles: $preset -> $db"
    & $importer $source $db --preset $preset --seed $Seed
    if ($LASTEXITCODE) { throw "PuzzleImporter failed for $preset" }
}
exit 0
