$ErrorActionPreference = 'Stop'
$source = '\\wsl.localhost\Ubuntu-24.04\home\openion\Valence-Net\build'
$target = 'E:\VM\Share\Valence-rtl\memory-lab-20261025-selective'
if(Test-Path -LiteralPath $target) {throw 'Refusing overwrite'}
New-Item -ItemType Directory -Path ($target+'\queue'),($target+'\ultra') | Out-Null
Copy-Item -Path ($source+'\memory-queue-rtl\*.sv') -Destination ($target+'\queue')
Copy-Item -Path ($source+'\ramstyle-lab\build\ultra\*.sv') -Destination ($target+'\ultra')
$memory = $target+'\ultra\memory_1024x128.sv'
$rtl = [IO.File]::ReadAllText($memory).Replace('reg [127:0] Memory[0:1023];','(* ram_style = "ultra" *) reg [127:0] Memory[0:1023];')
[IO.File]::WriteAllText($memory,$rtl)
Copy-Item -LiteralPath ($source+'\ramstyle-lab\synth.tcl'),($source+'\ramstyle-lab\queue_tb.sv') -Destination $target
Start-Process -FilePath 'E:\Xilinx\2025.2\Vivado\bin\vivado.bat' -ArgumentList @('-mode','batch','-notrace','-source',($target+'\synth.tcl'),'-log',($target+'\vivado.log'),'-journal',($target+'\vivado.jou'),'-tclargs',$target.Replace('\','/')) -WorkingDirectory $target -WindowStyle Hidden -RedirectStandardOutput ($target+'\console.log') -RedirectStandardError ($target+'\stderr.log') -PassThru | Select-Object Id
