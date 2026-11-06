param([string]$RunDirectory='E:\VM\Share\Valence-rtl',
 [string]$VivadoBin='E:\Xilinx\2025.2\Vivado\bin')
$ErrorActionPreference='Stop'
$run=(Resolve-Path -LiteralPath $RunDirectory).ProviderPath
$test=Join-Path $run ('uart-log-test-'+(Get-Date -Format 'yyyyMMddTHHmmss'))
New-Item -ItemType Directory -Path $test | Out-Null
$source=Join-Path $run 'iperf_uart_log.sv'
if(-not (Test-Path -LiteralPath $source)){$source=Join-Path $PSScriptRoot 'iperf_uart_log.sv'}
Copy-Item -LiteralPath $source -Destination $test
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\sim\uart_log_tb.sv') -Destination $test
Push-Location $test
try {
 & (Join-Path $VivadoBin 'xvlog.bat') -sv iperf_uart_log.sv uart_log_tb.sv *> compile.log
 if($LASTEXITCODE){throw 'UART test compile failed'}
 & (Join-Path $VivadoBin 'xelab.bat') uart_log_tb -s uart_log_sim -debug typical -timescale 1ns/1ps -mt 4 *> elaborate.log
 if($LASTEXITCODE){throw 'UART test elaboration failed'}
 & (Join-Path $VivadoBin 'xsim.bat') uart_log_sim -runall *> test.log
 if($LASTEXITCODE -or -not (Select-String -LiteralPath test.log -Pattern 'UART_LOG_PASS' -Quiet)){
   Get-Content -LiteralPath test.log -Tail 20
   throw 'UART bit-level oracle failed'
 }
 $evidence=Join-Path $PSScriptRoot '..\build\uart'
 New-Item -ItemType Directory -Path $evidence -Force | Out-Null
 Copy-Item -LiteralPath 'test.log','compile.log','elaborate.log','iperf_uart_log.sv','uart_log_tb.sv' -Destination $evidence -Force
 $record=[ordered]@{
  passed=$true
  clock_hz=125000000
  nominal_baud=460800
  baud_div=271
  actual_baud=(125000000.0/271)
  heartbeat_test_cycles=5000000
  heartbeat_production_cycles=125000000
  input_sha256=@{
   'iperf_uart_log.sv'=(Get-FileHash -LiteralPath 'iperf_uart_log.sv' -Algorithm SHA256).Hash.ToLower()
   'uart_log_tb.sv'=(Get-FileHash -LiteralPath 'uart_log_tb.sv' -Algorithm SHA256).Hash.ToLower()
  }
  test_log_sha256=(Get-FileHash -LiteralPath 'test.log' -Algorithm SHA256).Hash.ToLower()
  run_directory=$test
  checks=@('nominal_460800_receiver','exact_271_clock_bit_edges','8N1_no_unknowns',
   'startup_address','atomic_snapshot','busy_change_coalescing','heartbeat',
   'mid_character_reset_and_header_replay','subsequent_state_change','diagnostic_word_mapping','atomic_diagnostic_line','diagnostic_heartbeat_only')
 }
 [IO.File]::WriteAllText((Join-Path $evidence 'verification.json'),($record|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
 Get-Content -LiteralPath test.log -Tail 10
 Write-Output $test
} finally {Pop-Location}
