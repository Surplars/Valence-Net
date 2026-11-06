param([string]$Port='COM4',[Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$null=New-Item -ItemType Directory -Path $OutputDirectory -Force
$directory=(Resolve-Path -LiteralPath $OutputDirectory).Path
$portObject=[IO.Ports.SerialPort]::new($Port,460800,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
$portObject.Handshake=[IO.Ports.Handshake]::None
$portObject.DtrEnable=$false; $portObject.RtsEnable=$false
$portObject.Encoding=[Text.Encoding]::ASCII
$utf8=[Text.UTF8Encoding]::new($false)
$rawPath=Join-Path $directory 'uart.log'
$writer=$null
$metadata=[ordered]@{started_utc=[DateTime]::UtcNow.ToString('o');port=$Port;baud=460800;data_bits=8;parity='none';stop_bits=1;flow_control='none';dtr=$false;rts=$false;board_build_seen=$null;characters=0;state='opening'}
try {
    $portObject.Open()
    $writer=[IO.StreamWriter]::new($rawPath,$false,$utf8); $writer.AutoFlush=$true
    $metadata['state']='capturing'
    [IO.File]::WriteAllText((Join-Path $directory 'serial-capture.json'),($metadata|ConvertTo-Json -Depth 4),$utf8)
    Write-Output ('UART_CAPTURE_READY '+$Port+' 460800 8N1 '+$directory)
    $pending=''; $bannerSeen=$false
    while (-not (Test-Path -LiteralPath (Join-Path $directory 'STOP'))) {
        $chunk=$portObject.ReadExisting()
        if ($chunk.Length) {
            $writer.Write($chunk); $metadata['characters']+=$chunk.Length
            $pending+=$chunk
            while ($pending.Contains("`n")) {
                $index=$pending.IndexOf("`n"); $line=$pending.Substring(0,$index).TrimEnd("`r"); $pending=$pending.Substring($index+1)
                if ($line -match 'BUILD=(TX64-RX(?:128K-N57A|256K-N57B))') {
                    $metadata['board_build_seen']=$Matches[1]; $metadata['banner_line']=$line; $metadata['banner_seen_utc']=[DateTime]::UtcNow.ToString('o')
                    if ($true) { Write-Output ('BOARD_BUILD_BANNER '+$line); $bannerSeen=$true }
                }
            }
            if ($pending.Length -gt 16384) { $pending=$pending.Substring($pending.Length-4096) }
            [IO.File]::WriteAllText((Join-Path $directory 'serial-capture.json'),($metadata|ConvertTo-Json -Depth 4),$utf8)
        }
        Start-Sleep -Milliseconds 25
    }
    $metadata['state']='completed'
} catch {
    $metadata['state']='failed';$metadata['error']=$_.Exception.Message
    throw
} finally {
    if ($writer) {$writer.Dispose()}
    if ($portObject.IsOpen) {$portObject.Close()};$portObject.Dispose()
    $metadata['ended_utc']=[DateTime]::UtcNow.ToString('o')
    [IO.File]::WriteAllText((Join-Path $directory 'serial-capture.json'),($metadata|ConvertTo-Json -Depth 4),$utf8)
}