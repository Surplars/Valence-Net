# Immutable OOC baseline, invoked with the absolute run directory as argv[0].
# All input/output paths use the native WSL workspace, exposed to Windows Vivado.
if {$argc != 1} {error "usage: ooc.tcl RUN_DIRECTORY"}
set run [file normalize [lindex $argv 0]]
set part xczu15eg-ffvb1156-2-i
set period 6.4
if {[llength [get_parts -quiet $part]] != 1} {error "Target part is not installed: $part"}
set_param general.maxThreads 8
set result [open [file join $run results.tsv] w]
puts $result "top\tsetup_wns_ns\thold_whs_ns\tregister_setup_ns\tregister_hold_ns\tblackboxes\tstatus"
flush $result
proc worst_slack {kind args} {
    set path [get_timing_paths -quiet -delay_type $kind -max_paths 1 {*}$args]
    if {[llength $path] == 0} {return NA}
    return [get_property SLACK $path]
}
set tops {TcpDataTransmit TcpManagedConnection TcpReceive EthernetUdpEndpoint}
if {[file exists [file join $run targets.txt]]} {
    set targets_file [open [file join $run targets.txt] r]
    set tops [split [string trim [read $targets_file]] "\n"]
    close $targets_file
}
foreach top $tops {
    if {$top ni {TcpDataTransmit TcpManagedConnection TcpReceive EthernetUdpEndpoint}} {
        error "Unsupported module top: $top"
    }
    set work [file join $run $top]
    file mkdir $work
    cd $work
    puts "NETSTACK_OOC_BEGIN $top"
    set progress [open [file join $run progress.txt] w]
    puts $progress "$top synthesis"
    close $progress
    create_project -in_memory -part $part
    set files [concat [glob -nocomplain [file join $run input $top *.sv]] \
        [glob -nocomplain [file join $run input $top *.v]]]
    if {[llength $files] == 0} {error "Missing RTL for $top"}
    read_verilog -sv $files
    set xdc [open constraints.xdc w]
    puts $xdc {create_clock -name stack_clock -period 6.400 [get_ports clock]}
    puts $xdc {set_clock_uncertainty -setup 0.100 [get_clocks stack_clock]}
    puts $xdc {set_clock_uncertainty -hold 0.050 [get_clocks stack_clock]}
    puts $xdc {set_input_delay -clock stack_clock -max 1.000 [get_ports -filter {DIRECTION == IN && NAME != clock}]}
    puts $xdc {set_input_delay -clock stack_clock -min 0.000 [get_ports -filter {DIRECTION == IN && NAME != clock}]}
    puts $xdc {set_output_delay -clock stack_clock -max 1.000 [get_ports -filter {DIRECTION == OUT}]}
    puts $xdc {set_output_delay -clock stack_clock -min 0.000 [get_ports -filter {DIRECTION == OUT}]}
    close $xdc
    read_xdc constraints.xdc
    synth_design -top $top -part $part -mode out_of_context -flatten_hierarchy rebuilt
    if {[get_property PERIOD [get_clocks stack_clock]] != $period} {error "Clock constraint missing"}
    # OOC does not contain the board's actual clock buffer. Fix its assumed
    # source site so hold analysis has an explicit, reproducible clock origin.
    if {[llength [get_sites -quiet BUFGCE_X0Y0]] != 1} {error "Clock source site missing"}
    set_property HD.CLK_SRC BUFGCE_X0Y0 [get_ports clock]
    report_utilization -hierarchical -file post_synth_utilization.rpt
    report_timing_summary -delay_type min_max -report_unconstrained -check_timing_verbose -file post_synth_timing.rpt
    write_checkpoint -force post_synth.dcp
    set progress [open [file join $run progress.txt] w]
    puts $progress "$top placement/routing"
    close $progress
    opt_design
    place_design
    phys_opt_design
    route_design
    report_route_status -file post_route_status.rpt
    report_utilization -hierarchical -file post_route_utilization.rpt
    report_timing_summary -delay_type min_max -report_unconstrained -check_timing_verbose -file post_route_timing.rpt
    report_timing -delay_type max -max_paths 30 -nworst 3 -input_pins -file post_route_setup_paths.rpt
    report_timing -delay_type min -max_paths 20 -nworst 2 -input_pins -file post_route_hold_paths.rpt
    set registers [all_registers -clock stack_clock]
    report_timing -from $registers -to $registers -delay_type max -max_paths 20 -nworst 2 \
        -input_pins -file post_route_register_paths.rpt
    report_drc -file post_route_drc.rpt
    report_methodology -file post_route_methodology.rpt
    write_checkpoint -force post_route.dcp
    set setup [worst_slack max]
    set hold [worst_slack min]
    set register_setup [worst_slack max -from $registers -to $registers]
    set register_hold [worst_slack min -from $registers -to $registers]
    set blackboxes [llength [get_cells -quiet -hier -filter {IS_BLACKBOX}]]
    set status [expr {$setup ne "NA" && $hold ne "NA" && $setup >= 0 && $hold >= 0 && $blackboxes == 0 ? "TIMING_MET" : "TIMING_NOT_MET"}]
    puts $result [join [list $top $setup $hold $register_setup $register_hold $blackboxes $status] "\t"]
    flush $result
    puts "NETSTACK_OOC_RESULT $top $status setup=$setup hold=$hold register_setup=$register_setup"
    close_project
}
close $result
set progress [open [file join $run progress.txt] w]
puts $progress "completed"
close $progress
puts "NETSTACK_OOC_COMPLETE: module results only; not integrated SoC or board signoff"
