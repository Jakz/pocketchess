# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
  Zips a folder's contents with '/' entry separators, so the archive unpacks the same on Windows,
  macOS and Linux. Windows PowerShell 5.1's Compress-Archive and ZipFile::CreateFromDirectory
  write '\'. Used by projects/msvc/build.ps1 and projects/pocketbook/build-zig.ps1 (-Package).
.EXAMPLE
  scripts/zip-folder.ps1 -Folder out/package-windows -Zip out/PocketChess-windows-Release.zip
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Folder,
    [Parameter(Mandatory)][string]$Zip,
    # A list of file names that are left out (projects/not-redistributable.txt; '#' comments).
    [string]$ExcludeFile = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$root = (Resolve-Path -LiteralPath $Folder).Path.TrimEnd('\') + '\'
$excluded = @()
if ($ExcludeFile) {
    $excluded = @(Get-Content -LiteralPath $ExcludeFile | ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') })
}
if (Test-Path -LiteralPath $Zip) { Remove-Item -LiteralPath $Zip }
$archive = [IO.Compression.ZipFile]::Open($Zip, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($item in Get-ChildItem -LiteralPath $root -Recurse -Force) {
        $name = $item.FullName.Substring($root.Length).Replace('\', '/')
        if ($item.PSIsContainer) {
            # Empty folders (imports/, logs/ ...) are kept as directory entries.
            if (-not (Get-ChildItem -LiteralPath $item.FullName -Force)) { [void]$archive.CreateEntry($name + '/') }
        } elseif ($excluded -contains $item.Name) {
            Write-Host "zip-folder: not packaged (not redistributable): $name"
        } else {
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $item.FullName, $name,
                [IO.Compression.CompressionLevel]::Optimal)
        }
    }
} finally {
    $archive.Dispose()
}
exit 0
