param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$directory=[IO.Path]::GetFullPath($OutputDirectory)
$admin=Join-Path $PSScriptRoot 'interrupt_ab_admin.ps1'
$arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$admin+'"'),'-OutputDirectory',('"'+$directory+'"'))
$process=Start-Process -FilePath 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe' -Verb RunAs -WindowStyle Hidden -ArgumentList $arguments -PassThru
Write-Output ('INTERRUPT_AB_ADMIN_PID '+$process.Id)
$deadline=[DateTime]::UtcNow.AddMinutes(3)
while(-not (Test-Path -LiteralPath (Join-Path $directory 'READY'))){
 if(Test-Path -LiteralPath (Join-Path $directory 'FAILED.json')){throw (Get-Content -LiteralPath (Join-Path $directory 'FAILED.json') -Raw)}
 if([DateTime]::UtcNow -ge $deadline){throw 'Timed out waiting for temporary NIC setting'}
 Start-Sleep -Milliseconds 250
}
try {
 $deadline=[DateTime]::UtcNow.AddSeconds(25)
 do {& ping.exe -n 1 -w 1000 192.168.137.10|Out-Null;if($LASTEXITCODE -eq 0){break};Start-Sleep -Milliseconds 500}while([DateTime]::UtcNow -lt $deadline)
 if($LASTEXITCODE){throw 'Board did not respond after adapter restart'}
 Write-Output 'INTERRUPT_MODERATION_TEMPORARILY_OFF'
 & 'E:\VM\Share\Valence-rtl\valence-iperf-20261025T071436Z\measure_iperf3.ps1' -Seconds 5 -OutputDirectory (Join-Path $directory 'interrupt-off')
} finally {
 [IO.File]::WriteAllText((Join-Path $directory 'STOP'),[DateTime]::UtcNow.ToString('o'))
 $deadline=[DateTime]::UtcNow.AddSeconds(90)
 while(-not (Test-Path -LiteralPath (Join-Path $directory 'RESTORED.json'))){
  if(Test-Path -LiteralPath (Join-Path $directory 'RESTORE_FAILED.json')){throw (Get-Content -LiteralPath (Join-Path $directory 'RESTORE_FAILED.json') -Raw)}
  if([DateTime]::UtcNow -ge $deadline){throw 'NIC restoration was not confirmed'}
  Start-Sleep -Milliseconds 250
 }
 Write-Output 'ALL_NIC_ADVANCED_PROPERTIES_RESTORED'
}
Start-Sleep -Seconds 3
$deadline=[DateTime]::UtcNow.AddSeconds(25)
do {& ping.exe -n 1 -w 1000 192.168.137.10|Out-Null;if($LASTEXITCODE -eq 0){break};Start-Sleep -Milliseconds 500}while([DateTime]::UtcNow -lt $deadline)
if($LASTEXITCODE){throw 'Board did not respond after NIC restoration'}
& 'E:\VM\Share\Valence-rtl\valence-iperf-20261025T071436Z\measure_iperf3.ps1' -Seconds 5 -OutputDirectory (Join-Path $directory 'interrupt-restored')
Write-Output ('INTERRUPT_AB_COMPLETE '+$directory)