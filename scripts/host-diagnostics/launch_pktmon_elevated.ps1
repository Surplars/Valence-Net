param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$directory=[IO.Path]::GetFullPath($OutputDirectory)
$allowed='C:\Users\surplars\Documents\Codex\2026-10-23\https-github-com-surplars-valence-tree-3\outputs\'
if(-not $directory.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected output directory'}
$null=New-Item -ItemType Directory -Path $directory -Force
try {
 Start-Transcript -Path (Join-Path $directory 'elevated-transcript.txt') -Force | Out-Null
 & (Join-Path $PSScriptRoot 'capture_valence_pktmon.ps1') -OutputDirectory $directory
} catch { $_|Out-File -LiteralPath (Join-Path $directory 'FAILED.txt');exit 1 }
finally { Stop-Transcript -ErrorAction SilentlyContinue|Out-Null }