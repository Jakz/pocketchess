# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
  Builds the content packs in data/generated/ from data/downloads/ (architecture doc sec. 12).
  Both build scripts stage data/generated/ into content/ when it exists (otherwise the small
  packs in data/samples/). No network access: run scripts/fetch-lichess.ps1 first. Uses the
  PuzzleImporter and PgnImporter built by the MSVC solution.
.EXAMPLE
  scripts/build-content.ps1                       # content/puzzles/large.db (50k puzzles)
  scripts/build-content.ps1 -Presets starter,large
  scripts/build-content.ps1 -Presets all        # every Lichess puzzle (~6.1 million, ~450 MB)
  scripts/build-content.ps1 -Presets @() -Games   # content/games/lichess-2013-01.db (20k games)
  scripts/build-content.ps1 -Presets @() -Historical                 # content/games/historical.db (fetch-historical.ps1)
  scripts/build-content.ps1 -Presets @() -Historical -ToYear 1999 -MinElo 2300
#>
[CmdletBinding()]
param(
    [ValidateSet('starter', 'standard', 'large', 'huge', 'all')][string[]]$Presets = @('large'),
    [switch]$Games,
    [int]$MaxGames = 20000,
    [switch]$Historical,          # over-the-board games from scripts/fetch-historical.ps1
    [int]$FromYear = 0,
    [int]$ToYear = 0,
    [int]$MinElo = 0,
    [string]$Configuration = 'Desktop',
    [uint64]$Seed = 1
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$downloads = Join-Path $RepoRoot 'data\downloads'

function Tool([string]$name) {
    $exe = Join-Path $RepoRoot "out\msvc\$Configuration\$name.exe"
    if (-not (Test-Path $exe)) { throw "$name not built: $exe (projects/msvc/build.ps1 -Configuration $Configuration)" }
    return $exe
}

if ($Presets.Count -gt 0) {
    $importer = Tool 'PuzzleImporter'
    $source = Join-Path $downloads 'lichess_db_puzzle.csv.zst'
    if (-not (Test-Path $source)) { throw "No puzzle download: $source (scripts/fetch-lichess.ps1)" }
    $out = Join-Path $RepoRoot 'data\generated\puzzles'
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    foreach ($preset in $Presets) {
        $db = Join-Path $out "$preset.db"
        Write-Host "== puzzles: $preset -> $db"
        & $importer $source $db --preset $preset --seed $Seed
        if ($LASTEXITCODE) { throw "PuzzleImporter failed for $preset" }
    }
}

if ($Games) {
    # A CC0 Lichess month (scripts/fetch-lichess.ps1 -Url https://database.lichess.org/standard/lichess_db_standard_rated_2013-01.pgn.zst).
    $importer = Tool 'PgnImporter'
    $source = Join-Path $downloads 'lichess_db_standard_rated_2013-01.pgn.zst'
    if (-not (Test-Path $source)) { throw "No games download: $source (see the comment above)" }
    $db = Join-Path $RepoRoot 'data\generated\games\lichess-2013-01.db'
    Write-Host "== games: $MaxGames from $source -> $db"
    & $importer $source $db --max $MaxGames --name "Lichess January 2013"
    if ($LASTEXITCODE) { throw 'PgnImporter failed' }
}

if ($Historical) {
    $importer = Tool 'PgnImporter'
    $sources = @(
        (Join-Path $downloads 'pgnmentor/pgn'),
        (Join-Path $downloads 'lumbra/pgn')
    ) | Where-Object { Test-Path $_ }
    if (-not $sources) { throw 'No historical games: run scripts/fetch-historical.ps1 first' }
    $db = Join-Path $RepoRoot 'data/generated/games/historical.db'
    Write-Host "== historical games: $($sources -join ', ') -> $db"
    $opts = @('--name', 'Historical games', '--skip-online')
    if ($FromYear) { $opts += @('--from-year', $FromYear) }
    if ($ToYear) { $opts += @('--to-year', $ToYear) }
    if ($MinElo) { $opts += @('--min-elo', $MinElo) }
    & $importer @sources $db @opts
    if ($LASTEXITCODE) { throw 'PgnImporter failed' }
}
exit 0
