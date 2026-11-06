param([Parameter(Mandatory=$true)][string]$CaptureDirectory)
$ErrorActionPreference='Stop'
$wrapper=Join-Path $PSScriptRoot 'launch_pktmon_elevated.ps1'
$arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$wrapper+'"'),'-OutputDirectory',('"'+$CaptureDirectory+'"'))
$process=Start-Process -FilePath 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe' -Verb RunAs -WindowStyle Hidden -ArgumentList $arguments -PassThru
Write-Output ('Elevated capture PID '+$process.Id)
$deadline=[DateTime]::UtcNow.AddMinutes(3)
while (-not (Test-Path -LiteralPath (Join-Path $CaptureDirectory 'READY'))) {
 if(Test-Path -LiteralPath (Join-Path $CaptureDirectory 'FAILED.txt')){Get-Content -LiteralPath (Join-Path $CaptureDirectory 'pktmon-commands.log');throw (Get-Content -LiteralPath (Join-Path $CaptureDirectory 'FAILED.txt') -Raw)}
 if([DateTime]::UtcNow -ge $deadline){throw 'Timed out waiting for capture'}
 Start-Sleep -Milliseconds 250
}
if(Test-Path -LiteralPath (Join-Path $CaptureDirectory 'DONE')){throw 'Capture already ended'}
Write-Output 'Filtered capture ready; starting original Windows iperf3'
try {
 $ErrorActionPreference='Continue'
 $raw=@(& 'D:\TOOLS\ENV\iperf-3.22-win64\iperf3.exe' -c 192.168.137.10 -t 5 -J -l 32K -N -w 1M 2> (Join-Path $CaptureDirectory 'iperf3.stderr.txt'))
 $code=$LASTEXITCODE;$ErrorActionPreference='Stop'
 [IO.File]::WriteAllText((Join-Path $CaptureDirectory 'iperf3.json'),($raw -join [Environment]::NewLine),[Text.UTF8Encoding]::new($false))
 Write-Output ('IPERF_EXIT '+$code);Start-Sleep -Seconds 1
} finally {[IO.File]::WriteAllText((Join-Path $CaptureDirectory 'STOP'),[DateTime]::UtcNow.ToString('o'))}
$deadline=[DateTime]::UtcNow.AddMinutes(1)
while(-not (Test-Path -LiteralPath (Join-Path $CaptureDirectory 'DONE'))){
 if(Test-Path -LiteralPath (Join-Path $CaptureDirectory 'FAILED.txt')){throw (Get-Content -LiteralPath (Join-Path $CaptureDirectory 'FAILED.txt') -Raw)}
 if([DateTime]::UtcNow -ge $deadline){throw 'Capture conversion not completed'}
 Start-Sleep -Milliseconds 250
}
Write-Output ('CAPTURE_TEST_FINISHED '+$CaptureDirectory)