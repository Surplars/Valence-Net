param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop';$null=New-Item -ItemType Directory -Path $OutputDirectory -Force
$utf8=[Text.UTF8Encoding]::new($false);$rows=@()
$cases=@(@{name='128K-w1M';extra=@('-l','128K','-w','1M')},@{name='1M-default-buffer';extra=@('-l','1M')},@{name='1M-w4M';extra=@('-l','1M','-w','4M')},@{name='32K-w1M-nagle';extra=@('-l','32K','-w','1M')})
foreach($case in $cases){
 $arguments=@('-c','192.168.137.10','-t','5','-J')+$case.extra
 Write-Output ('WINDOWS_WRITE_BATCH '+$case.name)
 $ErrorActionPreference='Continue';$raw=@(& 'D:\TOOLS\ENV\iperf-3.22-win64\iperf3.exe' @arguments 2> (Join-Path $OutputDirectory ($case.name+'.stderr.txt')));$code=$LASTEXITCODE;$ErrorActionPreference='Stop'
 $text=$raw -join [Environment]::NewLine;[IO.File]::WriteAllText((Join-Path $OutputDirectory ($case.name+'.json')),$text,$utf8)
 $j=$text|ConvertFrom-Json
 $row=[ordered]@{case=$case.name;arguments=$arguments;exit_code=$code;error=$j.error;receiver_mbps=$j.end.sum_received.bits_per_second/1e6;sender_mbps=$j.end.sum_sent.bits_per_second/1e6;cpu=$j.end.cpu_utilization_percent.host_total;actual_send_buffer=$j.start.sndbuf_actual}
 $rows+=$row;[IO.File]::WriteAllText((Join-Path $OutputDirectory 'summary.json'),($rows|ConvertTo-Json -Depth 5),$utf8);Write-Output ($row|ConvertTo-Json -Compress -Depth 5);Start-Sleep -Seconds 1
}