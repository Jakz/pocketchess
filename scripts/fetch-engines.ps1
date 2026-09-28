# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
  Restores local engine sources and networks from their pins (D15, build doc sec. 2):
  downloads the tag's source archive, verifies SHA-256, unpacks into libs/<id>/, then downloads
  the NNUE nets named in src/evaluate.h and checks the hash prefix encoded in each net's name.
  Builds never call this script; it and fetch-lichess.ps1 are the only ones using the network.
.EXAMPLE
  scripts/fetch-engines.ps1                 # every engine in projects/engines.txt
  scripts/fetch-engines.ps1 -Engine stockfish -Pin   # record the archive SHA-256 in engine.lock
#>
[CmdletBinding()]
param([string]$Engine = '', [switch]$Pin, [switch]$Force)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Read-Lock([string]$path) {
    $h = @{}
    foreach ($line in Get-Content -LiteralPath $path) {
        $l = $line.Trim()
        if (-not $l -or $l.StartsWith('#')) { continue }
        $k, $v = $l -split '\s*=\s*', 2
        $h[$k] = $v
    }
    return $h
}

$ids = if ($Engine) { @($Engine) } else {
    @(Get-Content (Join-Path $RepoRoot 'projects\engines.txt') | ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') })
}
$tmp = Join-Path $RepoRoot 'out\downloads'
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

foreach ($id in $ids) {
    $lockPath = Join-Path $RepoRoot "projects\pocketbook\engines\$id\engine.lock"
    if (-not (Test-Path $lockPath)) { throw "No pin for '$id' ($lockPath)" }
    $lock = Read-Lock $lockPath
    $dest = Join-Path $RepoRoot "libs\$id"
    if ((Test-Path (Join-Path $dest 'src')) -and -not $Force -and -not $Pin) {
        Write-Host "${id}: libs\$id already present (use -Force to re-download)"
    } else {
        $archive = Join-Path $tmp "$id-$($lock.tag).zip"
        Write-Host "${id}: downloading $($lock.source_url)"
        Invoke-WebRequest -Uri $lock.source_url -OutFile $archive -UseBasicParsing
        $hash = (Get-FileHash -Algorithm SHA256 $archive).Hash.ToLowerInvariant()
        if ($lock.source_sha256 -and $lock.source_sha256 -ne 'TBD' -and $lock.source_sha256.ToLowerInvariant() -ne $hash) {
            throw "${id}: SHA-256 mismatch for the source archive (expected $($lock.source_sha256), got $hash)"
        }
        if ($Pin -or $lock.source_sha256 -eq 'TBD') {
            (Get-Content -LiteralPath $lockPath) -replace '^source_sha256\s*=.*$', "source_sha256 = $hash" | Set-Content -LiteralPath $lockPath
            Write-Host "${id}: pinned source_sha256 = $hash (commit engine.lock)"
        }
        $unpack = Join-Path $tmp "$id-unpack"
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $unpack, $dest
        Expand-Archive -LiteralPath $archive -DestinationPath $unpack
        $top = Get-ChildItem -LiteralPath $unpack -Directory | Select-Object -First 1
        Move-Item -LiteralPath $top.FullName -Destination $dest
        Remove-Item -Recurse -Force $unpack
    }

    # Networks named in evaluate.h, verified by the SHA-256 prefix in their names.
    $evalH = Join-Path $dest 'src\evaluate.h'
    if (Test-Path $evalH) {
        $nets = [regex]::Matches((Get-Content -LiteralPath $evalH -Raw), 'nn-([0-9a-f]{12})\.nnue') | ForEach-Object { $_.Value } | Sort-Object -Unique
        foreach ($net in $nets) {
            $file = Join-Path $dest "src\$net"
            $prefix = $net.Substring(3, 12)
            $ok = (Test-Path $file) -and ((Get-FileHash -Algorithm SHA256 $file).Hash.ToLowerInvariant().StartsWith($prefix))
            if (-not $ok) {
                foreach ($base in @($lock.nets_base_url, $lock.nets_fallback_url)) {
                    if (-not $base) { continue }
                    try {
                        Write-Host "${id}: downloading $net from $base"
                        Invoke-WebRequest -Uri ($base + $net) -OutFile $file -UseBasicParsing
                        if ((Get-FileHash -Algorithm SHA256 $file).Hash.ToLowerInvariant().StartsWith($prefix)) { $ok = $true; break }
                        Write-Warning "${net}: hash does not match its name, trying the next mirror"
                    } catch { Write-Warning "${net}: $($_.Exception.Message)" }
                }
            }
            if (-not $ok) { throw "${id}: could not obtain a verified $net" }
            Write-Host "${id}: $net verified"
        }
    }
    Write-Host "${id}: ready in libs\$id"
}
exit 0
