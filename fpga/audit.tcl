# Read-only post-route audit. Run from a completed frozen run directory.
set root [file normalize [lindex $argv 0]]
cd $root
set_param general.maxThreads 8
open_checkpoint routed.dcp
set rx_phase_file [open audit_rx_phase.txt w]
puts $rx_phase_file [get_property CLKOUT0_PHASE [get_cells rx_clocks/mmcm]]
close $rx_phase_file
report_timing_summary -delay_type min_max -report_unconstrained -max_paths 20 -file audit_timing.rpt
report_bus_skew -file audit_bus_skew.rpt
check_timing -verbose -file audit_check_timing.rpt
report_clock_interaction -file audit_clock_interaction.rpt
report_exceptions -coverage -file audit_exceptions.rpt
report_cdc -details -file audit_cdc.rpt
report_drc -file audit_drc.rpt
set setup [get_timing_paths -delay_type max -max_paths 1]
set hold [get_timing_paths -delay_type min -max_paths 1]
set f [open audit_metrics.txt w]
puts $f "setup_slack_ns=[get_property SLACK $setup]"
puts $f "hold_slack_ns=[get_property SLACK $hold]"
puts $f "board_iperf_measured=false"
close $f
