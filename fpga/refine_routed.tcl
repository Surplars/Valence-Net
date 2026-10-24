# Locally refine an immutable, fully routed checkpoint.
# Signoff stays at 6.4 ns, setup/hold uncertainty 0.100/0.050 ns.
# Later attempts may use stricter optimization targets, then restore signoff.
if {$argc != 2 || [lindex $argv 1] ne "route"} {
    error "usage: refine_routed.tcl SNAPSHOT_DIRECTORY route"
}
set run [file normalize [lindex $argv 0]]
set top ValenceNetTimingTop
set work [file join $run $top]
file mkdir $work
cd $work
set_param general.maxThreads 8
open_checkpoint [file join $run input routed_reference.dcp]
if {[get_property PART [current_design]] ne "xczu15eg-ffvb1156-2-i"} {
    error "Reference part mismatch"
}
if {[llength [get_clocks stack_clock]] != 1 ||
    [get_property PERIOD [get_clocks stack_clock]] != 6.4} {
    error "Reference clock mismatch"
}
proc progress {run message} {
    set channel [open [file join $run progress.txt] w]
    puts $channel $message
    close $channel
}
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
proc timing_is_clean {filename} {
    report_timing_summary -delay_type min_max -report_unconstrained -check_timing_verbose -file $filename
    set channel [open $filename r]
    set contents [read $channel]
    close $channel
    foreach line [split $contents "\n"] {
        set fields [regexp -all -inline {\S+} $line]
        if {[llength $fields] == 12 && [string is double -strict [lindex $fields 0]]} {
            return [expr {
                [lindex $fields 0] >= 0 && [lindex $fields 1] == 0 && [lindex $fields 2] == 0 &&
                [lindex $fields 4] >= 0 && [lindex $fields 5] == 0 && [lindex $fields 6] == 0 &&
                [lindex $fields 8] >= 0 && [lindex $fields 9] == 0 && [lindex $fields 10] == 0
            }]
        }
    }
    error "Missing timing summary"
}
if {![route_is_clean reference_route_status.rpt]} {
    error "Reference checkpoint is not fully routed"
}
set attempts [open refinement-attempts.tsv w]
puts $attempts "attempt\toptimization_setup_uncertainty_ns\tsignoff_setup_ns\tsignoff_hold_ns\trouting_clean\ttiming_clean"
set attempt 0
set clean_route 0
set clean_timing 0
foreach optimization_uncertainty {0.100 0.150 0.200} {
    incr attempt
    progress $run "$top local routed refinement attempt $attempt"
    puts "NETSTACK_LOCAL_REFINE attempt=$attempt optimization_uncertainty=$optimization_uncertainty signoff_uncertainty=0.100"
    set_clock_uncertainty -setup $optimization_uncertainty [get_clocks stack_clock]
    set_clock_uncertainty -hold 0.050 [get_clocks stack_clock]
    phys_opt_design -directive AggressiveExplore
    set_clock_uncertainty -setup 0.100 [get_clocks stack_clock]
    set_clock_uncertainty -hold 0.050 [get_clocks stack_clock]
    set clean_route [route_is_clean [format "attempt-%s-route.rpt" $attempt]]
    set clean_timing [timing_is_clean [format "attempt-%s-timing.rpt" $attempt]]
    puts $attempts [join [list $attempt $optimization_uncertainty [worst_slack max] [worst_slack min] $clean_route $clean_timing] "\t"]
    flush $attempts
    # Accept only a completely routed result. If optimization invalidates
    # any route, preserve the checkpoint for explicit routing repair.
    if {!$clean_route || $clean_timing} {break}
}
close $attempts
set clean_route [route_is_clean post_route_status.rpt]
report_utilization -hierarchical -file post_route_utilization.rpt
set clean_timing [timing_is_clean post_route_timing.rpt]
report_timing -delay_type max -max_paths 30 -nworst 3 -input_pins -file post_route_setup_paths.rpt
report_timing -delay_type min -max_paths 20 -nworst 2 -input_pins -file post_route_hold_paths.rpt
set registers [all_registers -clock stack_clock]
report_timing -from $registers -to $registers -delay_type max -max_paths 20 -nworst 2 -input_pins -file post_route_register_paths.rpt
report_timing -from $registers -to $registers -delay_type min -max_paths 20 -nworst 2 -input_pins -file post_route_register_hold_paths.rpt
report_drc -file post_route_drc.rpt
report_methodology -file post_route_methodology.rpt
write_checkpoint -force post_route.dcp
set setup [worst_slack max]
set hold [worst_slack min]
set register_setup [worst_slack max -from $registers -to $registers]
set register_hold [worst_slack min -from $registers -to $registers]
set blackboxes [llength [get_cells -quiet -hier -filter {IS_BLACKBOX}]]
set status [expr {$clean_route && $clean_timing && $setup ne "NA" && $hold ne "NA" &&
    $setup >= 0 && $hold >= 0 && $blackboxes == 0 ? "TIMING_MET" : "TIMING_NOT_MET"}]
set result [open [file join $run results.tsv] w]
puts $result "top\tsetup_wns_ns\thold_whs_ns\tregister_setup_ns\tregister_hold_ns\tblackboxes\tstatus"
puts $result [join [list $top $setup $hold $register_setup $register_hold $blackboxes $status] "\t"]
close $result
puts "NETSTACK_OOC_RESULT $top $status setup=$setup hold=$hold routing_clean=$clean_route"
progress $run completed
close_project
puts "NETSTACK_OOC_COMPLETE: local routed refinement; not SoC or board signoff"
