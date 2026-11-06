# Reuse fixed routing to inspect the actual RX capture eye without rebuilding.
set baseline [file normalize [lindex $argv 0]]
set output [file normalize [lindex $argv 1]]
cd $output
set_param general.maxThreads 8
open_checkpoint [file join $baseline routed.dcp]
set mmcm [get_cells rx_clocks/mmcm]
set saved_phase [get_property CLKOUT0_PHASE $mmcm]
set f [open phase_metrics.csv w]
puts $f "phase_degrees,rx_setup_ns,rx_hold_ns,global_setup_ns,global_hold_ns,rx_waveform"
foreach phase {33.750 39.375 45.000 50.625 56.250} {
 set_property CLKOUT0_PHASE $phase [get_cells rx_clocks/mmcm]
 set path [get_timing_paths -from [get_ports {eth_rxd[*] eth_rx_ctl}] -delay_type max -max_paths 1]
 if {[llength $path]!=1} {error "No RX setup path"}
 set setup [get_property SLACK $path]
 set path [get_timing_paths -from [get_ports {eth_rxd[*] eth_rx_ctl}] -delay_type min -max_paths 1]
 if {[llength $path]!=1} {error "No RX hold path"}
 set hold [get_property SLACK $path]
 set path [get_timing_paths -delay_type max -max_paths 1]
 set global_setup [get_property SLACK $path]
 set path [get_timing_paths -delay_type min -max_paths 1]
 set global_hold [get_property SLACK $path]
 set waveform [get_property WAVEFORM [get_clocks rx_source]]
 puts $f "$phase,$setup,$hold,$global_setup,$global_hold,$waveform"
 flush $f
 puts "RX_PHASE_METRIC phase=$phase setup=$setup hold=$hold global_setup=$global_setup global_hold=$global_hold waveform=$waveform"
 report_timing -from [get_ports {eth_rxd[*] eth_rx_ctl}] -delay_type min_max -max_paths 10 -file "rx_phase_${phase}.rpt"
}
set_property CLKOUT0_PHASE $saved_phase [get_cells rx_clocks/mmcm]
close $f
