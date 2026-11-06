param([Parameter(Mandatory=$true)][string]$RunDirectory,
 [string]$TargetSerial='210251A08870',
 [switch]$Program,
 [string]$VivadoPath='E:\Xilinx\2025.2\Vivado\bin\vivado.bat')
$ErrorActionPreference='Stop'
$run=(Resolve-Path -LiteralPath $RunDirectory).ProviderPath
$manifest=Get-Content -LiteralPath (Join-Path $run 'physical-verification.json') -Raw | ConvertFrom-Json
$bit=Join-Path $run 'valence_iperf.bit'
$actual=(Get-FileHash -LiteralPath $bit -Algorithm SHA256).Hash.ToLower()
if($actual -ne $manifest.output_sha256.'valence_iperf.bit'){throw 'Bitstream hash differs from sealed qualification'}
if($manifest.rx_capture_phase_degrees -ne 45){throw 'This helper requires the qualified RX45 candidate'}
if($TargetSerial -notmatch '^[0-9A-Za-z]+$'){throw 'Invalid target serial'}
$mode=if($Program){'PROGRAM'}else{'READ_ONLY'}
$test=Join-Path $run ('jtag-'+$mode.ToLower()+'-'+(Get-Date -Format 'yyyyMMddTHHmmss'))
New-Item -ItemType Directory -Path $test | Out-Null
$tcl=@'
set run [file normalize [lindex $argv 0]]
set serial [lindex $argv 1]
set perform [expr {[lindex $argv 2] eq "PROGRAM"}]
open_hw_manager
connect_hw_server -url TCP:127.0.0.1:3121
set result [catch {
 set targets {}
 foreach target [get_hw_targets] {
  if {[string match "*/Digilent/$serial" [get_property NAME $target]]} {lappend targets $target}
 }
 if {[llength $targets]!=1} {error "Expected exactly one requested Digilent target"}
 current_hw_target [lindex $targets 0]
 open_hw_target
 set candidates {}
 foreach device [get_hw_devices] {
  puts "DEVICE [get_property NAME $device] PART=[get_property PART $device]"
  set part [string tolower [get_property PART $device]]
  # Hardware Manager reports the board's JTAG part as xczu15, without EG/package.
  if {$part eq "xczu15" || [string match "xczu15eg*" $part]} {lappend candidates $device}
 }
 if {[llength $candidates]!=1} {error "Expected exactly one XCZU15EG device; refusing another part"}
 set device [lindex $candidates 0]
 set record [open device-before.txt w]
 puts $record "NAME=[get_property NAME $device] PART=[get_property PART $device]"
 close $record
 if {$perform} {
  set_property PROGRAM.FILE [file join $run valence_iperf.bit] $device
  program_hw_devices $device
  set record [open device-after.txt w]
  puts $record "NAME=[get_property NAME $device] PART=[get_property PART $device] FILE=[file join $run valence_iperf.bit]"
  close $record
  puts "VOLATILE_PROGRAM_PASS target=$serial device=[get_property NAME $device] file=[file join $run valence_iperf.bit]"
 } else {puts "READ_ONLY_DEVICE_PASS target=$serial device=[get_property NAME $device]"}
} message options]
catch {close_hw_target}
catch {disconnect_hw_server}
catch {close_hw_manager}
if {$result} {return -options $options $message}
'@
[IO.File]::WriteAllText((Join-Path $test 'program.tcl'),$tcl,[Text.UTF8Encoding]::new($false))
Push-Location $test
try {
 & $VivadoPath -mode batch -source (Join-Path $test 'program.tcl') -log jtag.log -journal jtag.jou -tclargs $run $TargetSerial $mode
 if($LASTEXITCODE){throw 'JTAG action failed; inspect this run log'}
} finally {Pop-Location}
Write-Output $test
