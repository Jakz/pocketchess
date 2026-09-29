# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
  Downloads the Lichess puzzle database (CC0) into data/downloads/ (M4, build doc sec. 2).
  Besides fetch-engines.ps1 this is the only script that uses the network; builds never call it.
  The file is replaced upstream every few weeks, so there is no fixed pin: the script writes
  <file>.info (URL, size, SHA-256, Last-Modified, download date) and PuzzleImporter copies that
  into the pack's meta table, so every puzzles.db says which download it came from.
.EXAMPLE
  scripts/fetch-lichess.ps1            # skips the download when the file is already there
  scripts/fetch-lichess.ps1 -Force     # fetch the current upstream file again
#>
[CmdletBinding()]
param(
    [string]$Url = 'https://database.lichess.org/lichess_db_puzzle.csv.zst',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$dir = Join-Path $RepoRoot 'data\downloads'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$file = Join-Path $dir (Split-Path $Url -Leaf)
$info = "$file.info"

if ((Test-Path $file) -and -not $Force) {
    Write-Host "lichess: $file already present (use -Force to download the current one)"
    if (Test-Path $info) { Get-Content $info | Write-Host }
    exit 0
}

Write-Host "lichess: downloading $Url"
$part = "$file.part"
$response = Invoke-WebRequest -Uri $Url -OutFile $part -UseBasicParsing -PassThru
Move-Item -Force $part $file
$item = Get-Item $file
$hash = (Get-FileHash -Algorithm SHA256 $file).Hash.ToLowerInvariant()
$modified = $response.Headers['Last-Modified']
@(
    "url = $Url",
    "size = $($item.Length)",
    "sha256 = $hash",
    "last_modified = $modified",
    "downloaded = $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))",
    'license = CC0 (https://database.lichess.org/#puzzles)'
) | Set-Content -Encoding ascii $info
Write-Host ("lichess: {0:N1} MB, sha256 {1}" -f ($item.Length / 1MB), $hash)
exit 0
