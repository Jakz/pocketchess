# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
  Milestone gate (build doc sec. 6.3, milestones DoD 1): source/layering checks, MSVC Debug + Release
  with all tests, the PocketBook Release build with ELF checks. Run before closing a milestone.
.EXAMPLE
  scripts/verify-all.ps1              # everything
  scripts/verify-all.ps1 -SkipPocketBook
#>
[CmdletBinding()]
param([switch]$SkipPocketBook, [switch]$Slow)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$results = New-Object System.Collections.Generic.List[string]
$sw = [Diagnostics.Stopwatch]::StartNew()

function Step([string]$name, [scriptblock]$body) {
    Write-Host "`n=== $name" -ForegroundColor Cyan
    $t = [Diagnostics.Stopwatch]::StartNew()
    & $body
    if ($LASTEXITCODE) { throw "$name failed ($LASTEXITCODE)" }
    $results.Add(("{0,-40} ok  ({1:N0} s)" -f $name, $t.Elapsed.TotalSeconds))
}

Step 'check-sources' { & (Join-Path $RepoRoot 'scripts\check-sources.ps1') }
Step 'MSVC Debug (build + tests)' { & (Join-Path $RepoRoot 'projects\msvc\build.ps1') -Configuration Debug -Target Test }
Step 'MSVC Release (build + tests)' { & (Join-Path $RepoRoot 'projects\msvc\build.ps1') -Configuration Release -Target Test }
if ($Slow) {
    Step 'Deep perft (Release)' {
        Push-Location $RepoRoot
        try { & (Join-Path $RepoRoot 'out\msvc\Release\PocketChessTests.exe') --slow --filter perft } finally { Pop-Location }
    }
}
if (-not $SkipPocketBook) {
    Step 'PocketBook Release (+ ELF checks)' { & (Join-Path $RepoRoot 'projects\pocketbook\build-zig.ps1') -Configuration Release }
}

Write-Host "`n=== verify-all: PASSED in $([int]$sw.Elapsed.TotalSeconds) s" -ForegroundColor Green
$results | ForEach-Object { Write-Host "  $_" }
Write-Host "Next: run the device checklist (docs/ai/05-testing-and-verification.md sec. 3) and update docs/project-status.md."
exit 0
