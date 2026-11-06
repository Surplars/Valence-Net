set root [file normalize [lindex $argv 0]]
set phase [lindex $argv 1]
set build_status [catch {
if {$phase ni {synth route bit}} {error "phase must be synth, route or bit"}
cd $root
set_param general.maxThreads 8
set reuse_routed [file exists imported_routed.dcp]
if {$reuse_routed} {
 if {$phase eq "synth"} {error "Routed reuse requires route or bit phase"}
 open_checkpoint imported_routed.dcp
 source $root/route_reuse.tcl
} elseif {[file exists imported_synth.dcp]} {
 open_checkpoint imported_synth.dcp
} else {
create_project -in_memory -part xczu15eg-ffvb1156-2-i
read_verilog -sv [glob $root/rtl/*.sv]
read_verilog -sv $root/iperf_board.sv
read_verilog -sv $root/iperf_uart_log.sv
read_verilog -sv $root/native_rgmii.sv
read_verilog -sv $root/native_gmac_clocks.sv
read_verilog -sv $root/native_gmac_divided_clock.sv
read_xdc $root/board.xdc
read_xdc $root/native_gmac_pins.xdc
synth_design -top iperf_board -part xczu15eg-ffvb1156-2-i -flatten_hierarchy rebuilt
write_checkpoint -force synth_unconstrained.dcp
}
if {!$reuse_routed} {
source $root/cdc.tcl
write_checkpoint -force synth.dcp
report_utilization -file synth_utilization.rpt
report_timing_summary -delay_type min_max -report_unconstrained -file synth_timing.rpt
report_cdc -details -file synth_cdc.rpt
report_drc -file synth_drc.rpt
}
if {$phase ne "synth"} {
 if {!$reuse_routed} {
 opt_design
 source $root/tx_boundary_seed.tcl
 write_checkpoint -force opt_seeded.dcp
 place_design -directive Explore
 write_checkpoint -force placed.dcp
 phys_opt_design -directive AggressiveExplore
 write_checkpoint -force post_place_physical.dcp
 route_design -directive AggressiveExplore -tns_cleanup
 }
 if {$reuse_routed} {
  set tx_seed_validate_only true
  source $root/tx_boundary_seed.tcl
 }
 # Preserve the routed state before any qualification guard can reject it.
 write_checkpoint -force routed_initial.dcp
 report_timing_summary -delay_type min_max -report_unconstrained -max_paths 100 -file routed_initial_timing.rpt
 set refinement 0
 foreach directive {AggressiveExplore AlternateReplication ExploreWithHoldFix AggressiveFanoutOpt} {
  set setup [get_timing_paths -delay_type max -max_paths 1]
  set hold [get_timing_paths -delay_type min -max_paths 1]
  if {[get_property SLACK $setup]>=0 && [get_property SLACK $hold]>=0} {break}
  incr refinement
  puts "POST_ROUTE_REFINEMENT pass=$refinement directive=$directive"
  phys_opt_design -directive $directive
  route_design -preserve -no_psir
  write_checkpoint -force refined_${refinement}.dcp
  report_timing_summary -delay_type min_max -report_unconstrained -max_paths 100 -file refined_${refinement}_timing.rpt
 }
 write_checkpoint -force routed.dcp
 report_timing_summary -delay_type min_max -report_unconstrained -file routed_timing.rpt
 report_utilization -file routed_utilization.rpt
 report_route_status -file route_status.rpt
 report_cdc -details -file routed_cdc.rpt
 report_drc -file routed_drc.rpt
 report_methodology -file routed_methodology.rpt
 assert_tx_boundary_seed
 if {$phase eq "bit"} {
  set setup [get_timing_paths -delay_type max -max_paths 1]
  set hold [get_timing_paths -delay_type min -max_paths 1]
  if {[get_property SLACK $setup]<0 || [get_property SLACK $hold]<0} {
   error "Timing failed: refusing to present a failing design as a board-ready bitstream"
  }
  write_bitstream -force valence_iperf.bit
 }
}
set f [open progress.txt w];puts $f "completed $phase";close $f
} build_message build_options]
if {$build_status} {
 set f [open [file join $root progress.txt] w]
 puts $f "failed $phase: $build_message"
 close $f
 return -options $build_options $build_message
}
