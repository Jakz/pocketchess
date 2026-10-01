# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
  Collects historical over-the-board games for a game pack (build-content.ps1 -Historical).
  Uses the network (like fetch-engines.ps1 and fetch-lichess.ps1); builds never call it.

  - PGN Mentor (pgnmentor.com, "completely free" downloads, no licence for redistribution): the
    player collections and the event files (tournaments, world championships); -Openings adds
    the opening collections. Files go to data/downloads/pgnmentor/ (ignored by Git) and stay on
    this PC and on your reader: they are never committed.
  - Lumbra's Gigabase (lumbrasgigabase.com, CC BY-NC-SA 4.0, personal use): its download manager
    has no stable links, so download the PGN .7z files yourself into data/downloads/lumbra/; this
    script extracts them with 7-Zip (C:\Program Files\7-Zip\7z.exe).

  Already downloaded files are skipped, so the script can be stopped and run again.
.EXAMPLE
  scripts/fetch-historical.ps1                  # PGN Mentor players + events, then any Lumbra .7z
  scripts/fetch-historical.ps1 -Openings        # also the opening collections
#>
[CmdletBinding()]
param(
    [switch]$Openings,
    [int]$DelayMs = 250,  # between downloads: be gentle with a free site
    [string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# --- PGN Mentor ------------------------------------------------------------------------------------
$base = 'https://www.pgnmentor.com/'
$root = Join-Path $RepoRoot 'data\downloads\pgnmentor'
$pgnDir = Join-Path $root 'pgn'
New-Item -ItemType Directory -Force -Path $pgnDir | Out-Null

Write-Host "pgnmentor: reading the file list"
$page = (Invoke-WebRequest -Uri ($base + 'files.html') -UseBasicParsing).Content
$sections = @('players', 'events')
if ($Openings) { $sections += 'openings' }
$links = [regex]::Matches($page, 'href="((?:players|events|openings)/[^"/]+\.(?:zip|pgn))"') |
    ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique |
    Where-Object { $sections -contains ($_ -split '/')[0] }
Write-Host ("pgnmentor: {0} files ({1})" -f $links.Count, ($sections -join ', '))

$new = 0; $failed = 0
foreach ($rel in $links) {
    $section, $name = $rel -split '/', 2
    $file = Join-Path (Join-Path $root $section) $name
    if (-not (Test-Path $file)) {
        New-Item -ItemType Directory -Force -Path (Split-Path $file) | Out-Null
        try {
            Invoke-WebRequest -Uri ($base + $rel) -OutFile "$file.part" -UseBasicParsing
            Move-Item -Force "$file.part" $file
            $new++
            Start-Sleep -Milliseconds $DelayMs
        } catch {
            Write-Warning "pgnmentor: $rel failed: $($_.Exception.Message)"
            Remove-Item -ErrorAction SilentlyContinue "$file.part"
            $failed++
            continue
        }
    }
    $target = Join-Path $pgnDir $section
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    if ($name.EndsWith('.zip')) {
        $marker = Join-Path $target ($name + '.extracted')
        if (-not (Test-Path $marker)) {
            Expand-Archive -Force -Path $file -DestinationPath (Join-Path $target ([IO.Path]::GetFileNameWithoutExtension($name)))
            New-Item -ItemType File -Force -Path $marker | Out-Null
        }
    } else {
        Copy-Item -Force $file (Join-Path $target $name)
    }
}
Write-Host "pgnmentor: $new new download(s), $failed failed; PGN files in $pgnDir"

# --- Lumbra's Gigabase ------------------------------------------------------------------------------
$lumbra = Join-Path $RepoRoot 'data\downloads\lumbra'
$archives = @(Get-ChildItem -ErrorAction SilentlyContinue -Path $lumbra -Filter *.7z -File)
if ($archives.Count -gt 0) {
    if (-not (Test-Path $SevenZip)) { throw "7-Zip not found at $SevenZip (pass -SevenZip <path to 7z.exe>)" }
    $out = Join-Path $lumbra 'pgn'
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    foreach ($a in $archives) {
        $marker = Join-Path $out ($a.Name + '.extracted')
        if (Test-Path $marker) { continue }
        Write-Host "lumbra: extracting $($a.Name)"
        & $SevenZip x -y "-o$out" $a.FullName | Out-Null
        if ($LASTEXITCODE) { throw "7-Zip failed on $($a.Name)" }
        New-Item -ItemType File -Force -Path $marker | Out-Null
    }
    Write-Host "lumbra: PGN files in $out"
} else {
    Write-Host "lumbra: no .7z in $lumbra (optional: download the PGN archives from lumbrasgigabase.com)"
}
exit 0
