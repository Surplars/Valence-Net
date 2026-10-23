# Route an immutable synthesized checkpoint without repeating synthesis.
if {$argc != 2} {error "usage: route_checkpoint.tcl SNAPSHOT_DIRECTORY TOP"}
set run [file normalize [lindex $argv 0]]
set top [lindex $argv 1]
if {$top ni {TcpDataTransmit TcpReceive TcpManagedConnection TcpIpv4Endpoint EthernetUdpEndpoint EthernetTcpUdpEndpoint}} {
    error "Unsupported top: $top"
}
if {![file exists [file join $run provenance.json]]} {error "Missing snapshot provenance"}
set source [file join $run $top post_synth.dcp]
if {![file exists $source]} {error "Missing synthesized checkpoint"}
set work [file join $run route-$top]
if {[file exists $work]} {error "Refusing to overwrite routed checkpoint"}
file mkdir $work
cd $work
set_param general.maxThreads 8
open_checkpoint $source
if {[llength [get_clocks stack_clock]] != 1 || [get_property PERIOD [get_clocks stack_clock]] != 6.4} {
    error "Checkpoint clock constraint mismatch"
}
set progress [open progress.txt w]
puts $progress "placement/routing"
close $progress
opt_design
place_design -directive Explore
phys_opt_design -directive AggressiveExplore
route_design -directive Explore
report_route_status -file post_route_status.rpt
report_utilization -hierarchical -file post_route_utilization.rpt
report_timing_summary -delay_type min_max -report_unconstrained -check_timing_verbose -file post_route_timing.rpt
report_timing -delay_type max -max_paths 30 -nworst 3 -input_pins -file post_route_setup_paths.rpt
report_timing -delay_type min -max_paths 20 -nworst 2 -input_pins -file post_route_hold_paths.rpt
set registers [all_registers -clock stack_clock]
report_timing -from $registers -to $registers -delay_type max -max_paths 20 -nworst 2 -input_pins -file post_route_register_paths.rpt
report_drc -file post_route_drc.rpt
report_methodology -file post_route_methodology.rpt
write_checkpoint -force post_route.dcp
set progress [open progress.txt w]
puts $progress "completed"
close $progress
set setup [get_property SLACK [get_timing_paths -quiet -delay_type max -max_paths 1]]
set hold [get_property SLACK [get_timing_paths -quiet -delay_type min -max_paths 1]]
puts "NETSTACK_ROUTED_RESULT top=$top setup=$setup hold=$hold"
