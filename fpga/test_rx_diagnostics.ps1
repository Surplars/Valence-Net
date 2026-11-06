param([Parameter(Mandatory=$true)][string]$RtlFile,
 [string]$RunDirectory='E:\VM\Share\Valence-rtl',
 [string]$VivadoBin='E:\Xilinx\2025.2\Vivado\bin')
$ErrorActionPreference='Stop'
$rtl=(Resolve-Path -LiteralPath $RtlFile).ProviderPath
$run=(Resolve-Path -LiteralPath $RunDirectory).ProviderPath
$test=Join-Path $run ('rx-diagnostics-test-'+(Get-Date -Format 'yyyyMMddTHHmmss'))
New-Item -ItemType Directory -Path $test | Out-Null
$rtlSources=@($rtl)
foreach($dependency in @('CdcMailbox.sv','CdcLevel.sv','CdcResetRelease.sv')){
 $rtlSources+=Join-Path (Split-Path -Parent $rtl) $dependency
}
$rtlHashes=@{}
$designText=foreach($source in $rtlSources){
 $rtlHashes[[IO.Path]::GetFileName($source)]=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLower()
 [IO.File]::ReadAllText($source)
}
[IO.File]::WriteAllText((Join-Path $test 'diagnostics_design.sv'),($designText -join "`n"),[Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\sim\rx_diagnostics_tb.sv') -Destination $test
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\src\main\scala\iperf\RxDiagnosticSnapshot.scala') -Destination $test
Push-Location $test
try {
 & (Join-Path $VivadoBin 'xvlog.bat') -sv diagnostics_design.sv rx_diagnostics_tb.sv *> compile.log
 if($LASTEXITCODE){throw 'RX diagnostics compile failed'}
 & (Join-Path $VivadoBin 'xelab.bat') rx_diagnostics_tb -s rx_diagnostics_sim -debug typical -timescale 1ns/1ps -mt 4 *> elaborate.log
 if($LASTEXITCODE){throw 'RX diagnostics elaboration failed'}
 & (Join-Path $VivadoBin 'xsim.bat') rx_diagnostics_sim -runall *> test.log
 if($LASTEXITCODE -or -not (Select-String -LiteralPath test.log -Pattern 'RX_DIAGNOSTICS_PASS' -Quiet)){
   Get-Content -LiteralPath test.log -Tail 20
   throw 'RX diagnostics CDC oracle failed'
 }
 $evidence=Join-Path $PSScriptRoot '..\build\rx-diagnostics'
 New-Item -ItemType Directory -Path $evidence -Force | Out-Null
 Copy-Item -LiteralPath 'test.log','compile.log','elaborate.log','rx_diagnostics_tb.sv','RxDiagnosticSnapshot.scala' -Destination $evidence -Force
 $record=[ordered]@{
  passed=$true
  source_clock_hz=125000000
  destination_clock_period_ns=13
  sample_cycles=125000
  counter_bits=32
  rtl_sha256=$rtlHashes
  input_sha256=@{
   'RxDiagnosticSnapshot.scala'=(Get-FileHash -LiteralPath 'RxDiagnosticSnapshot.scala' -Algorithm SHA256).Hash.ToLower()
   'rx_diagnostics_tb.sv'=(Get-FileHash -LiteralPath 'rx_diagnostics_tb.sv' -Algorithm SHA256).Hash.ToLower()
   'diagnostics_design.sv'=(Get-FileHash -LiteralPath 'diagnostics_design.sv' -Algorithm SHA256).Hash.ToLower()
  }
  test_log_sha256=(Get-FileHash -LiteralPath 'test.log' -Algorithm SHA256).Hash.ToLower()
  run_directory=$test
  checks=@('unrelated_clocks','atomic_closed_form_counter_oracle','simultaneous_events',
   'distinct_reason_totals','cumulative_and_longest_backpressure','no_initial_link_down',
   'inband_link_falling_edges','destination_clock_stopped_while_counting',
   'source_clock_stopped_holds_snapshot','common_reset_with_source_stopped','no_stale_epoch_replay')
 }
 [IO.File]::WriteAllText((Join-Path $evidence 'verification.json'),($record|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
 Get-Content -LiteralPath test.log -Tail 10
 Write-Output $test
} finally {Pop-Location}

