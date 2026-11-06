# Reuse the exact routed netlist and constraints; retry physical optimization only.
set root [file normalize [lindex $argv 0]]
set phase [lindex $argv 1]
if {$phase ne "bit"} {error "Timing retry expects bit phase"}
cd $root
set_param general.maxThreads 8
open_checkpoint imported_routed.dcp
if {[get_property CLKOUT0_PHASE [get_cells rx_clocks/mmcm]] != 45.0} {
 error "Unexpected RX capture phase in the imported checkpoint"
}
report_timing_summary -delay_type min_max -file retry_before_timing.rpt
phys_opt_design -directive AggressiveExplore
route_design -directive AggressiveExplore
write_checkpoint -force routed.dcp
report_timing_summary -delay_type min_max -report_unconstrained -file routed_timing.rpt
report_utilization -file routed_utilization.rpt
report_route_status -file route_status.rpt
report_cdc -details -file routed_cdc.rpt
report_drc -file routed_drc.rpt
report_methodology -file routed_methodology.rpt
set setup_slack [get_property SLACK [get_timing_paths -delay_type max -max_paths 1]]
set hold_slack [get_property SLACK [get_timing_paths -delay_type min -max_paths 1]]
if {$setup_slack < 0 || $hold_slack < 0} {
 error "Timing retry failed: setup=$setup_slack hold=$hold_slack; no bitstream produced"
}
write_bitstream -force valence_iperf.bit
set f [open progress.txt w]; puts $f "completed bit"; close $f
