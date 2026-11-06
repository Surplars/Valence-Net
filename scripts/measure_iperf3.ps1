param(
    [string]$ClientPath = 'D:\TOOLS\ENV\iperf-3.22-win64\iperf3.exe',
    [ValidateRange(1, 300)][int]$Seconds = 10,
    [string]$BoardBuild = 'TX64-RX256K-N57B',
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $ClientPath -PathType Leaf)) {
    $ClientPath = (Get-Command iperf3 -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
}
$ClientPath = (Resolve-Path -LiteralPath $ClientPath).Path
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $PSScriptRoot ('measurements\' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))
}
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$utf8 = New-Object System.Text.UTF8Encoding($false)
function Save-Json([string]$Name, $Value) {
    [System.IO.File]::WriteAllText((Join-Path $OutputDirectory $Name), ($Value | ConvertTo-Json -Depth 12), $utf8)
}
$metadata = [ordered]@{
    started_utc = [DateTime]::UtcNow.ToString('o')
    board_build_label = $BoardBuild
    board_build_verified_by_script = $false
    board_ip = '192.168.137.10'
    client_path = $ClientPath
    client_sha256 = (Get-FileHash -LiteralPath $ClientPath -Algorithm SHA256).Hash.ToLowerInvariant()
    client_version = @(& $ClientPath --version 2>&1 | ForEach-Object { "$_" })
    seconds_per_test = $Seconds
}
try {
    $ip = Get-NetIPAddress -AddressFamily IPv4 -IPAddress '192.168.137.1' -ErrorAction Stop | Select-Object -First 1
    $adapter = Get-NetAdapter -InterfaceIndex $ip.InterfaceIndex -ErrorAction Stop
    $metadata['adapter'] = @{ name = $adapter.Name; status = "$($adapter.Status)"; link_speed = $adapter.LinkSpeed; address = $ip.IPAddress; prefix_length = $ip.PrefixLength }
} catch { $metadata['adapter_read_error'] = $_.Exception.Message }
Save-Json 'metadata.json' $metadata
$cases = @(
    @{ name = 'forward-default'; extra = @() },
    @{ name = 'forward-32K-N'; extra = @('-l', '32K', '-N') },
    @{ name = 'reverse-default'; extra = @('-R') },
    @{ name = 'forward-32K-N-w1M'; extra = @('-l', '32K', '-N', '-w', '1M') },
    @{ name = 'reverse-w1M'; extra = @('-R', '-w', '1M') }
)
$summary = @()
foreach ($case in $cases) {
    $arguments = @('-c', '192.168.137.10', '-t', "$Seconds", '-J') + $case.extra
    Write-Host ("Running {0}: {1} {2}" -f $case.name, $ClientPath, ($arguments -join ' '))
    $stderr = Join-Path $OutputDirectory ($case.name + '.stderr.txt')
    $ErrorActionPreference = 'Continue'
    $lines = @(& $ClientPath @arguments 2> $stderr)
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    $raw = $lines -join [Environment]::NewLine
    [System.IO.File]::WriteAllText((Join-Path $OutputDirectory ($case.name + '.json')), $raw, $utf8)
    $row = [ordered]@{ test = $case.name; arguments = $arguments; exit_code = $code }
    try {
        $report = $raw | ConvertFrom-Json -ErrorAction Stop
        $row['sender_mbps'] = $report.end.sum_sent.bits_per_second / 1e6
        $row['receiver_mbps'] = $report.end.sum_received.bits_per_second / 1e6
        $row['iperf_error'] = $report.error
        $row['socket_send_buffer_bytes'] = $report.start.sndbuf_actual
        $row['socket_receive_buffer_bytes'] = $report.start.rcvbuf_actual
        $row['client_process_cpu_percent'] = $report.end.cpu_utilization_percent.host_total
        Write-Host ("{0}: sender {1:N1}, receiver {2:N1} Mbit/s; exit {3}" -f $case.name, $row.sender_mbps, $row.receiver_mbps, $code)
    } catch { $row['json_read_error'] = $_.Exception.Message }
    $summary += [pscustomobject]$row
    Save-Json 'summary.json' @($summary)
    Start-Sleep -Seconds 2
}
Write-Host "Logs saved to $OutputDirectory"
Write-Host 'Keep the UART log from startup through the end of these tests.'
