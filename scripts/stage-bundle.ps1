# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
  Stages the pocketchess/ data tree (build doc sec. 5) from projects/bundle-manifest.txt, so the
  Windows and PocketBook bundles cannot diverge. Never touches user/.
.EXAMPLE
  scripts/stage-bundle.ps1 -Target windows -Dest out\msvc\Debug\pocketchess
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('windows', 'pocketbook')][string]$Target,
    [Parameter(Mandatory)][string]$Dest,
    [string]$Configuration = 'Debug',
    [string]$Content = ''  # folder with generated puzzles.db/games.db (default data/generated)
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$engineTarget = if ($Target -eq 'windows') { 'windows-x64' } else { 'arm' }
$copied = 0

function Copy-IfNewer([string]$src, [string]$dst) {
    $dstDir = Split-Path -Parent $dst
    if (-not (Test-Path $dstDir)) { New-Item -ItemType Directory -Force -Path $dstDir | Out-Null }
    $s = Get-Item -LiteralPath $src
    if (Test-Path -LiteralPath $dst) {
        $d = Get-Item -LiteralPath $dst
        if ($d.Length -eq $s.Length -and $d.LastWriteTimeUtc -ge $s.LastWriteTimeUtc) { return }
    }
    Copy-Item -LiteralPath $src -Destination $dst -Force
    $script:copied++
}

New-Item -ItemType Directory -Force -Path $Dest | Out-Null
foreach ($d in 'imports', 'exports', 'logs') { New-Item -ItemType Directory -Force -Path (Join-Path $Dest $d) | Out-Null }

# 1. Manifest entries: "[target] <source> => <destination>" (target optional).
$manifest = Join-Path $RepoRoot 'projects/bundle-manifest.txt'
foreach ($raw in Get-Content -LiteralPath $manifest) {
    $line = $raw.Trim()
    if (-not $line -or $line.StartsWith('#')) { continue }
    if ($line -match '^\[(\w+)\]\s*(.*)$') {
        if ($Matches[1] -ne $Target) { continue }
        $line = $Matches[2]
    }
    $parts = $line -split '\s*=>\s*'
    if ($parts.Count -ne 2) { throw "bundle-manifest.txt: bad line '$raw'" }
    $src = Join-Path $RepoRoot $parts[0]
    $dst = Join-Path $Dest $parts[1]
    if ($parts[0].Contains('*')) {
        foreach ($f in Get-ChildItem -Path $src -File -ErrorAction SilentlyContinue) { Copy-IfNewer $f.FullName (Join-Path $dst $f.Name) }
    } elseif (Test-Path -LiteralPath $src) {
        Copy-IfNewer $src $dst
    } else {
        Write-Warning "stage-bundle: missing $($parts[0])"
    }
}

# 2. Engines listed in projects/engines.txt: descriptor + binary for this target.
$enginesTxt = Join-Path $RepoRoot 'projects/engines.txt'
$engineIds = @(Get-Content -LiteralPath $enginesTxt | ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') })
# Engines dropped from engines.txt leave the staged tree too (deploy.ps1 mirrors it to the reader).
$stagedEngines = Join-Path $Dest 'engines'
if (Test-Path $stagedEngines) {
    foreach ($d in Get-ChildItem -LiteralPath $stagedEngines -Directory | Where-Object { $engineIds -notcontains $_.Name }) {
        Remove-Item -Recurse -Force -LiteralPath $d.FullName
        Write-Host "stage-bundle: removed engine '$($d.Name)' (not in projects/engines.txt)"
    }
}
foreach ($id in $engineIds) {
    $yaml = Join-Path $RepoRoot "data/engines/$id/engine.yaml"
    if (Test-Path $yaml) { Copy-IfNewer $yaml (Join-Path $Dest "engines/$id/engine.yaml") }
    $binDir = Join-Path $RepoRoot "out/engines/$id/$engineTarget"
    if (Test-Path $binDir) {
        foreach ($f in Get-ChildItem -LiteralPath $binDir -File) { Copy-IfNewer $f.FullName (Join-Path $Dest "engines/$id/$($f.Name)") }
    } else {
        Write-Host "stage-bundle: engine '$id' not built for $engineTarget yet (projects/pocketbook/build-engine.ps1)"
    }
}

# 3. Content packs: generated puzzles when present, otherwise the small sample; games always ship the
#    curated sample (annotated classics) next to any generated pack.
if (-not $Content) { $Content = Join-Path $RepoRoot 'data/generated' }
foreach ($kind in 'puzzles', 'games') {
    $gen = Join-Path $Content $kind
    $sample = Join-Path $RepoRoot "data/samples/$kind"
    $froms = @()
    if (Test-Path $gen) { $froms += $gen }
    if ((Test-Path $sample) -and ($kind -eq 'games' -or $froms.Count -eq 0)) { $froms += $sample }
    $names = @()
    foreach ($from in $froms) {
        foreach ($f in Get-ChildItem -LiteralPath $from -Filter *.db -File) {
            Copy-IfNewer $f.FullName (Join-Path $Dest "content/$kind/$($f.Name)")
            $names += $f.Name
        }
    }
    # The bundle mirrors the sources: a pack removed or renamed there must not linger (it would ship).
    $staged = Join-Path $Dest "content/$kind"
    if (Test-Path $staged) {
        foreach ($old in Get-ChildItem -LiteralPath $staged -Filter *.db -File) {
            if ($names -notcontains $old.Name) {
                Remove-Item -LiteralPath $old.FullName
                Write-Host "stage-bundle: removed stale content/$kind/$($old.Name)"
            }
        }
    }
}

Write-Host "stage-bundle: $Target -> $Dest ($copied file(s) updated)"
exit 0
