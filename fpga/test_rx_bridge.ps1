param([Parameter(Mandatory=$true)][string]$RunDirectory,
 [string]$VivadoBin='E:\Xilinx\2025.2\Vivado\bin')
$ErrorActionPreference='Stop'
$run=(Resolve-Path -LiteralPath $RunDirectory).ProviderPath
$test=Join-Path $run ('rx-bridge-test-'+(Get-Date -Format 'yyyyMMddTHHmmss'))
New-Item -ItemType Directory -Path $test | Out-Null
Copy-Item -LiteralPath (Join-Path $run 'rtl') -Destination $test -Recurse
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\sim\rx_bridge_tb.sv') -Destination $test
Push-Location $test
try {
 $sources=@(Get-ChildItem -LiteralPath 'rtl' -Filter '*.sv' | ForEach-Object {Join-Path 'rtl' $_.Name})
 & (Join-Path $VivadoBin 'xvlog.bat') -sv @sources rx_bridge_tb.sv *> compile.log
 if($LASTEXITCODE){Get-Content compile.log -Tail 15;throw 'RX bridge compile failed'}
 & (Join-Path $VivadoBin 'xelab.bat') rx_bridge_tb -s rx_bridge_sim -debug typical -timescale 1ps/1ps -mt 4 *> elaborate.log
 if($LASTEXITCODE){Get-Content elaborate.log -Tail 15;throw 'RX bridge elaboration failed'}
 & (Join-Path $VivadoBin 'xsim.bat') rx_bridge_sim -runall *> test.log
 if($LASTEXITCODE -or -not (Select-String -LiteralPath test.log -Pattern 'RX_BRIDGE_PASS' -Quiet)){
  Get-Content test.log -Tail 15;throw 'RX bridge oracle failed'
 }
 $hashes=@{}
 foreach($source in $sources){$hashes[[IO.Path]::GetFileName($source)]=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLower()}
 $record=[ordered]@{
  passed=$true;scope='Actual generated GMII MAC, async bridge and width adapter; no physical RGMII';
  frames=1024;bytes=1550336;rx_half_periods_ps=@(4001,3999);tx_period_ps=8000;
  minimum_ifg_cycles=12;backpressure_cycles_per_1024=200;
  rtl_sha256=$hashes;
  testbench_sha256=(Get-FileHash rx_bridge_tb.sv -Algorithm SHA256).Hash.ToLower();
  test_log_sha256=(Get-FileHash test.log -Algorithm SHA256).Hash.ToLower();run_directory=$test
 }
 $evidence=Join-Path $PSScriptRoot '..\build\rx-bridge'
 New-Item -ItemType Directory -Path $evidence -Force | Out-Null
 Copy-Item -LiteralPath 'test.log','compile.log','elaborate.log','rx_bridge_tb.sv' -Destination $evidence -Force
 [IO.File]::WriteAllText((Join-Path $evidence 'verification.json'),($record|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
 Get-Content test.log -Tail 10
 Write-Output $test
} finally {Pop-Location}
