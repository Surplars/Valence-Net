# Immutable OOC baseline, invoked with the absolute run directory as argv[0].
# Run on a frozen Windows-local snapshot; WSL owns the source and simulation.
if {$argc < 1 || $argc > 2} {error "usage: ooc.tcl RUN_DIRECTORY ?synth|route?"}
set phase [expr {$argc == 2 ? [lindex $argv 1] : "route"}]
if {$phase ni {synth route}} {error "Unknown phase: $phase"}
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
proc route_is_clean {filename} {
    report_route_status -file $filename
    set channel [open $filename r]
    set contents [read $channel]
    close $channel
    foreach {variable label} {
        routable {routable nets}
        routed {fully routed nets}
        errors {nets with routing errors}
    } {
        set pattern [format {# of %s\.+\s*:\s*(\d+)} $label]
        if {![regexp $pattern $contents -> $variable]} {error "Missing route count: $label"}
    }
    return [expr {$routable > 0 && $routable == $routed && $errors == 0}]
}
set tops {TcpDataTransmit TcpManagedConnection TcpReceive EthernetUdpEndpoint}
if {[file exists [file join $run targets.txt]]} {
    set targets_file [open [file join $run targets.txt] r]
    set tops [split [string trim [read $targets_file]] "\n"]
    close $targets_file
}
foreach top $tops {
    if {$top ni {TcpDataTransmit TcpConnectionTransmit TcpIpv4Endpoint TcpManagedConnection TcpReceive EthernetUdpEndpoint EthernetTcpUdpEndpoint MultiTcpIpv4Endpoint MultiEthernetTcpUdpEndpoint TcpRetransmissionTimer ValenceNetTimingTop}} {
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
    # XDC accepts timing commands, not Tcl conditionals. The timing fixture
    # has only a clock input; direct module OOC has actual data inputs.
    if {$top ne "ValenceNetTimingTop"} {
        puts $xdc {set_input_delay -clock stack_clock -max 1.000 [get_ports -filter {DIRECTION == IN && NAME != clock}]}
        puts $xdc {set_input_delay -clock stack_clock -min 0.000 [get_ports -filter {DIRECTION == IN && NAME != clock}]}
    }
    puts $xdc {set_output_delay -clock stack_clock -max 1.000 [get_ports -filter {DIRECTION == OUT}]}
    puts $xdc {set_output_delay -clock stack_clock -min 0.000 [get_ports -filter {DIRECTION == OUT}]}
    close $xdc
    read_xdc constraints.xdc
    synth_design -top $top -part $part -mode out_of_context -flatten_hierarchy rebuilt
    if {[get_property PERIOD [get_clocks stack_clock]] != $period} {error "Clock constraint missing"}
    # OOC does not contain the board's actual clock buffer. Fix its assumed
    # source site so hold analysis has an explicit, reproducible clock origin.
    if {[llength [get_sites -quiet BUFGCE_X0Y0]] != 1} {error "Clock source site missing"}
    if {$top eq "ValenceNetTimingTop"} {
        if {[llength [get_cells -quiet coreClockBuffer]] != 1} {error "Physical clock buffer missing"}
        set_property LOC BUFGCE_X0Y0 [get_cells coreClockBuffer]
    } else {
        set_property HD.CLK_SRC BUFGCE_X0Y0 [get_ports clock]
    }
    report_utilization -hierarchical -file post_synth_utilization.rpt
    report_timing_summary -delay_type min_max -report_unconstrained -check_timing_verbose -file post_synth_timing.rpt
    report_timing -delay_type max -max_paths 20 -nworst 2 -input_pins -file post_synth_setup_paths.rpt
    write_checkpoint -force post_synth.dcp
    if {$phase eq "synth"} {
        puts $result [join [list $top [worst_slack max] [worst_slack min] NA NA [llength [get_cells -quiet -hier -filter {IS_BLACKBOX}]] SYNTH_ONLY] "\t"]
        flush $result
        close_project
        continue
    }
    set progress [open [file join $run progress.txt] w]
    puts $progress "$top placement/routing"
    close $progress
    opt_design
    place_design
    report_timing_summary -delay_type min_max -report_unconstrained -check_timing_verbose -file post_place_timing.rpt
    report_timing -delay_type max -max_paths 30 -nworst 3 -input_pins -file post_place_setup_paths.rpt
    write_checkpoint -force post_place.dcp
    phys_opt_design
    report_timing_summary -delay_type min_max -report_unconstrained -check_timing_verbose -file post_physopt_timing.rpt
    report_timing -delay_type max -max_paths 30 -nworst 3 -input_pins -file post_physopt_setup_paths.rpt
    write_checkpoint -force post_physopt.dcp
    route_design
    # Preserve a failed baseline, then allow one standard physical-refinement
    # attempt without rebuilding RTL. Successful baseline timing is untouched.
    set baseline_setup [worst_slack max]
    set baseline_hold [worst_slack min]
    if {$baseline_setup ne "NA" && $baseline_hold ne "NA" &&
        ($baseline_setup < 0 || $baseline_hold < 0)} {
        report_timing_summary -delay_type min_max -report_unconstrained -check_timing_verbose -file baseline_route_timing.rpt
        report_timing -delay_type max -max_paths 30 -nworst 3 -input_pins -file baseline_route_setup_paths.rpt
        write_checkpoint -force baseline_route.dcp
        puts "NETSTACK_POST_ROUTE_REFINE $top setup=$baseline_setup hold=$baseline_hold"
        set progress [open [file join $run progress.txt] w]
        puts $progress "$top post-route physical refinement"
        close $progress
        phys_opt_design -directive AggressiveExplore
        report_timing_summary -delay_type min_max -report_unconstrained -check_timing_verbose -file local_refinement_timing.rpt
        write_checkpoint -force local_refinement.dcp
        # A local physical optimization may already leave every net routed.
        # Preserve that result instead of ripping it up in a global reroute.
        if {![route_is_clean local_refinement_route_status.rpt]} {
            route_design -directive Explore
        }
    }
    report_route_status -file post_route_status.rpt
    report_utilization -hierarchical -file post_route_utilization.rpt
    report_timing_summary -delay_type min_max -report_unconstrained -check_timing_verbose -file post_route_timing.rpt
    report_timing -delay_type max -max_paths 30 -nworst 3 -input_pins -file post_route_setup_paths.rpt
    report_timing -delay_type min -max_paths 20 -nworst 2 -input_pins -file post_route_hold_paths.rpt
    set registers [all_registers -clock stack_clock]
    report_timing -from $registers -to $registers -delay_type max -max_paths 20 -nworst 2 \
        -input_pins -file post_route_register_paths.rpt
    report_timing -from $registers -to $registers -delay_type min -max_paths 20 -nworst 2 \
        -input_pins -file post_route_register_hold_paths.rpt
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
