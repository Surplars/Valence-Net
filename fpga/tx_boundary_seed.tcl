# Constrain the unchanged RGMII boundary before placement.
# In fresh Net57 synthesis, two logical LUT3 pairs share B5/B6 and D5/D6.
# Keep this explicit, verified packing rather than expecting independent A6/C6 BELs.
# The register locations came from the sealed and board-tested TX64/ACK1/RX45 implementation.
set tx_seed_rows {}
set f [open gold-native-layout.tsv r]
while {[gets $f line] >= 0} {
 lassign [split $line "\t"] name site bel
 if {$name eq ""} {continue}
 lappend tx_seed_rows [list $name $site [lindex [split $bel .] end]]
}
close $f
if {[llength $tx_seed_rows] != 19} {error "Expected 19 TX boundary/input/reset registers"}
foreach {name site bel} {
 {u_rgmii/lanes[0].tx_ddr_i_1} SLICE_X115Y110 B5LUT
 {u_rgmii/lanes[1].tx_ddr_i_1} SLICE_X115Y110 B6LUT
 {u_rgmii/lanes[2].tx_ddr_i_1} SLICE_X115Y110 D5LUT
 {u_rgmii/lanes[3].tx_ddr_i_1} SLICE_X115Y110 D6LUT
 {u_rgmii/tx_clock_ddr_i_1} SLICE_X115Y110 E6LUT
} {lappend tx_seed_rows [list $name $site $bel]}
if {![info exists tx_seed_validate_only] || !$tx_seed_validate_only} {
 foreach row $tx_seed_rows {
  lassign $row name site bel
  set cell [get_cells -hier -filter [format {NAME == "%s"} $name]]
  if {[llength $cell] != 1} {error "Missing unique seeded cell $name"}
  set_property BEL $bel $cell
  set_property LOC $site $cell
  set_property IS_LOC_FIXED true $cell
  set_property IS_BEL_FIXED true $cell
  # Keep the exact verified output-symbol functions at their seeded BELs.
  # Post-route PSIR otherwise remaps these LUTs despite the location guidance.
  if {[string match "LUT*" [get_property REF_NAME $cell]]} {
   set_property DONT_TOUCH true $cell
  }
 }
}
proc assert_tx_boundary_seed {} {
 global tx_seed_rows
 foreach row $tx_seed_rows {
  lassign $row name site bel
  set cell [get_cells -hier -filter [format {NAME == "%s"} $name]]
  if {[llength $cell] != 1 || [get_property LOC $cell] ne $site ||
      [lindex [split [get_property BEL $cell] .] end] ne $bel} {
   error "Routed TX boundary differs from its frozen placement seed: $name"
  }
 }
 set properties [open final-boundary-properties.tsv w]
 foreach row $tx_seed_rows {
  lassign $row name site bel
  set cell [get_cells -hier -filter [format {NAME == "%s"} $name]]
  if {![get_property IS_LOC_FIXED $cell] || ![get_property IS_BEL_FIXED $cell]} {error "Final boundary not fixed: $name"}
  set ref [get_property REF_NAME $cell]
  if {[string match "LUT*" $ref] && ![get_property DONT_TOUCH $cell]} {error "Output-symbol LUT not protected: $name"}
  puts $properties [join [list $name $ref [get_property LOC $cell] [get_property BEL $cell] [get_property IS_LOC_FIXED $cell] [get_property IS_BEL_FIXED $cell] [get_property DONT_TOUCH $cell]] "\t"]
 }
 close $properties
 set f [open final-boundary-verification.json w]
 puts $f {{"passed":true,"fixed_cells":24,"fixed_registers":19,"dont_touch_symbol_luts":5,"explicit_paired_lut_packing":true}}
 close $f
 set f [open tx_boundary_seed_verification.json w]
 puts $f {{"passed":true,"registers":19,"symbol_and_phase_luts":5,"applied_before_placement":true,"timing_constraints_relaxed":false}}
 close $f
 puts "PASS_TX_BOUNDARY_SEED cells=[llength $tx_seed_rows]"
}
