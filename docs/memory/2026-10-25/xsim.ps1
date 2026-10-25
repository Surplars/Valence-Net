$ErrorActionPreference='Stop'
$target='E:\VM\Share\Valence-rtl\memory-lab-20261025-selective'
Set-Location -LiteralPath $target
& 'E:\Xilinx\2025.2\Vivado\bin\xvlog.bat' 'queue_netlist.v' 'queue_tb.sv' 'E:\Xilinx\2025.2\Vivado\data\verilog\src\glbl.v' *> 'xvlog-console.log'
if($LASTEXITCODE -ne 0){throw 'xvlog failed'}
& 'E:\Xilinx\2025.2\Vivado\bin\xelab.bat' '-L' 'unisims_ver' '-L' 'secureip' 'queue_tb' 'glbl' '-s' 'mapped_queue' *> 'xelab-console.log'
if($LASTEXITCODE -ne 0){throw 'xelab failed'}
& 'E:\Xilinx\2025.2\Vivado\bin\xsim.bat' 'mapped_queue' '-runall' *> 'xsim-console.log'
if($LASTEXITCODE -ne 0){throw 'xsim failed'}
if(-not(Select-String -LiteralPath 'xsim-console.log' -SimpleMatch 'MAPPED_MEMORY_QUEUE_PASS' -Quiet)){throw 'No pass result'}

& 'E:\Xilinx\2025.2\Vivado\bin\xvlog.bat' 'ultra_netlist.v' 'ultra_store_tb.sv' *> 'ultra-xvlog-console.log'
if($LASTEXITCODE -ne 0){throw 'ultra xvlog failed'}
& 'E:\Xilinx\2025.2\Vivado\bin\xelab.bat' '-L' 'unisims_ver' '-L' 'secureip' 'ultra_store_tb' 'glbl' '-s' 'mapped_ultra' *> 'ultra-xelab-console.log'
if($LASTEXITCODE -ne 0){throw 'ultra xelab failed'}
& 'E:\Xilinx\2025.2\Vivado\bin\xsim.bat' 'mapped_ultra' '-runall' *> 'ultra-xsim-console.log'
if($LASTEXITCODE -ne 0){throw 'ultra xsim failed'}
if(-not(Select-String -LiteralPath 'ultra-xsim-console.log' -SimpleMatch 'MAPPED_ULTRA_STORE_PASS' -Quiet)){throw 'No URAM pass result'}
'passed' | Set-Content -LiteralPath 'mapped-memory-result.txt'
