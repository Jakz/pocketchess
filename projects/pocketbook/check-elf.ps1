# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
  Sanity checks for device binaries (build doc sec. 4.2 step 4, R5):
  ELF32, e_machine == 40 (ARM), soft-float ABI flag set and hard-float flag clear.
  With llvm-readelf (VS "C++ Clang tools" or PATH): only allowed NEEDED libraries and the
  highest GLIBC_2.x symbol version <= -MaxGlibc (the device's glibc, recorded in M0.5).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string[]]$Path,
    [string[]]$AllowedNeeded = @('libinkview.so', 'libc.so.6', 'libm.so.6', 'libpthread.so.0', 'ld-linux.so.3', 'libdl.so.2', 'librt.so.1'),
    [string]$MaxGlibc = ''   # e.g. '2.23'; empty = report only
)

$ErrorActionPreference = 'Stop'
$failed = $false

function Find-ReadElf {
    $cmd = Get-Command 'llvm-readelf' -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -prerelease -products * -property installationPath | Select-Object -First 1
        $c = Join-Path $vs 'VC\Tools\Llvm\x64\bin\llvm-readelf.exe'
        if (Test-Path $c) { return $c }
    }
    return $null
}

$readelf = Find-ReadElf
foreach ($p in $Path) {
    if (-not (Test-Path $p)) { Write-Host "check-elf: missing $p" -ForegroundColor Red; $failed = $true; continue }
    $b = [System.IO.File]::ReadAllBytes($p)
    $name = Split-Path -Leaf $p
    $ok = $b.Length -gt 52 -and $b[0] -eq 0x7F -and $b[1] -eq 0x45 -and $b[2] -eq 0x4C -and $b[3] -eq 0x46
    $class = $b[4]
    $machine = [BitConverter]::ToUInt16($b, 0x12)
    $flags = [BitConverter]::ToUInt32($b, 0x24)
    $soft = ($flags -band 0x200) -ne 0   # EF_ARM_ABI_FLOAT_SOFT
    $hard = ($flags -band 0x400) -ne 0   # EF_ARM_ABI_FLOAT_HARD
    $msg = "{0}: ELF{1} machine={2} flags=0x{3:X8} soft-float={4} hard-float={5}" -f $name, ($(if ($class -eq 1) { 32 } else { 64 })), $machine, $flags, $soft, $hard
    if (-not $ok -or $class -ne 1 -or $machine -ne 40 -or -not $soft -or $hard) {
        Write-Host "check-elf: FAIL $msg" -ForegroundColor Red
        $failed = $true
        continue
    }
    Write-Host "check-elf: ok   $msg"

    if ($readelf) {
        $dyn = & $readelf --dynamic $p 2>$null
        $needed = @($dyn | Select-String -Pattern 'Shared library: \[(.+?)\]' | ForEach-Object { $_.Matches[0].Groups[1].Value })
        foreach ($n in $needed) {
            if ($AllowedNeeded -notcontains $n) { Write-Host "check-elf: FAIL $name needs $n" -ForegroundColor Red; $failed = $true }
        }
        $versions = & $readelf --version-info $p 2>$null | Select-String -Pattern 'GLIBC_(\d+)\.(\d+)' -AllMatches |
            ForEach-Object { $_.Matches } | ForEach-Object { [version]("{0}.{1}" -f $_.Groups[1].Value, $_.Groups[2].Value) } | Sort-Object -Unique
        $max = if ($versions) { ($versions | Select-Object -Last 1).ToString() } else { 'none' }
        Write-Host "check-elf:      NEEDED: $($needed -join ', ')   max GLIBC: $max"
        if ($MaxGlibc -and $versions -and ([version]$max -gt [version]$MaxGlibc)) {
            Write-Host "check-elf: FAIL $name needs GLIBC_$max > device $MaxGlibc" -ForegroundColor Red
            $failed = $true
        }
    }
}
if (-not $readelf) { Write-Host 'check-elf: llvm-readelf not found; NEEDED/GLIBC checks skipped' }
if ($failed) { exit 1 }
exit 0
