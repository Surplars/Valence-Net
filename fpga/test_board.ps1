param([Parameter(Mandatory=$true)][string]$RunDirectory,
 [string]$VivadoBin='E:\Xilinx\2025.2\Vivado\bin')
$ErrorActionPreference='Stop'
$run=(Resolve-Path -LiteralPath $RunDirectory).ProviderPath
$test=Join-Path $run ('board-reset-test-'+(Get-Date -Format 'yyyyMMddTHHmmss'))
if(Test-Path -LiteralPath $test){throw 'Use a run without existing board reset test'}
New-Item -ItemType Directory -Path $test | Out-Null
Copy-Item -LiteralPath (Join-Path $run 'iperf_board.sv'),(Join-Path $run 'iperf_uart_log.sv'),(Join-Path $run 'native_rgmii.sv'),(Join-Path $run 'native_gmac_clocks.sv'),(Join-Path $run 'native_gmac_divided_clock.sv') -Destination $test
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\sim\board_reset_tb.sv') -Destination $test
Copy-Item -LiteralPath (Join-Path $VivadoBin '..\data\verilog\src\glbl.v') -Destination $test
Push-Location $test
try {
 & (Join-Path $VivadoBin 'xvlog.bat') -sv iperf_board.sv iperf_uart_log.sv native_rgmii.sv native_gmac_clocks.sv native_gmac_divided_clock.sv board_reset_tb.sv glbl.v *> compile.log
 if($LASTEXITCODE){throw 'Board boundary compile failed'}
 & (Join-Path $VivadoBin 'xelab.bat') -L unisims_ver -L unimacro_ver board_reset_tb glbl -s board_reset_sim -debug typical -timescale 1ns/1ps -mt 4 *> elaborate.log
 if($LASTEXITCODE){throw 'Board boundary elaboration failed'}
 & (Join-Path $VivadoBin 'xsim.bat') board_reset_sim -runall *> test.log
 if($LASTEXITCODE -or -not (Select-String -LiteralPath test.log -Pattern 'BOARD_RESET_PASS' -Quiet)){
   throw 'Board boundary test failed'
 }
 Get-Content -LiteralPath test.log -Tail 8
} finally {Pop-Location}
