# Physical-only ECO. The frozen TSV was read from the sealed TX32 checkpoint.
set fp [open gold-native-layout.tsv r]
set native_rows {}
set near_rows {}
set target_names {}
set target_sites {}
while {[gets $fp line] >= 0} {
 set fields [split $line "\t"]
 if {[llength $fields] < 3} {error "Invalid frozen TX placement row"}
 lassign $fields name site bel
 if {[string match "u_rgmii/quarter_tx_boundary.pad_*_reg*" $name]} {
  lappend native_rows [list $name $site [lindex [split $bel .] end]]
  lappend target_sites $site
 } elseif {[string match "u_rgmii/tx_data_reg*" $name] || $name eq "u_rgmii/tx_enable_reg" || $name eq "tx_sync_reg\[2\]"} {
  lappend near_rows [list $name $site [lindex [split $bel .] end]]
 } else {error "Unexpected placement target $name"}
 lappend target_names $name
}
close $fp
if {[llength $native_rows] != 9} {error "Expected nine retained physical TX boundary registers"}
if {[llength $near_rows] != 10} {error "Expected nine raw TX data/control registers and one reset-release register"}
proc tx_exact_cell {name} {
 set cells [get_cells -hier -filter [format {NAME == "%s"} $name]]
 if {[llength $cells] != 1} {error "Expected exactly one cell: $name"}
 return [lindex $cells 0]
}
set before_cells [llength [get_cells -hier]]
set before_nets [llength [get_nets -hier]]
set symbol_names [get_property NAME [get_cells -hier -filter {(NAME =~ "u_rgmii/lanes*.tx_ddr_i_1" || NAME == "u_rgmii/tx_clock_ddr_i_1") && REF_NAME =~ "LUT*"}]]
if {[llength $symbol_names] != 5} {error "Expected four TX symbol LUTs and the shared TX phase LUT"}
set move_names $target_names
# Free the FF clock/control resources in the two target slices. Relocate any
# displaced FFs individually into empty nearby slices; do not run the placer.
set displaced_rows {}
foreach site [lsort -unique $target_sites] {
 foreach cell [get_cells -quiet -of_objects [get_bels -of_objects [get_sites $site]]] {
  if {[string match "FD*" [get_property REF_NAME $cell]]} {
   set name [get_property NAME $cell]
   lappend move_names $name
   if {[lsearch -exact $target_names $name] < 0} {
    lappend displaced_rows [list $name [get_property LOC $cell] [lindex [split [get_property BEL $cell] .] end]]
   }
  }
 }
}
set move_names [lsort -unique [concat $move_names $symbol_names]]
set moved_clock_pins {}
foreach name $move_names {
 set cell [tx_exact_cell $name]
 if {[string match "FD*" [get_property REF_NAME $cell]]} {
  lappend moved_clock_pins [get_pins -of_objects $cell -filter {REF_PIN_NAME == C}]
 }
}
# Remove only the old sink branches, then freeze the remaining clock trunks.
# A partial FIXED_ROUTE lets the router add the new sink branches while keeping
# the insertion delays of the already signed-off core and PHY clock paths.
route_design -unroute -pins $moved_clock_pins
set preserved_clocks [open preserved_clock_routes.txt w]
foreach source {tx_clocks/raw_div/O tx_clocks/quarter_div/O} {
 set net [get_nets -of_objects [get_pins $source]]
 if {[llength $net] != 1} {error "Unexpected clock source net $source"}
 set trunk [get_property ROUTE $net]
 if {$trunk eq ""} {error "No clock trunk to preserve: $source"}
 set_property FIXED_ROUTE $trunk $net
 puts $preserved_clocks "[get_property NAME $net]\t$trunk"
}
close $preserved_clocks
foreach name $move_names {
 set cell [tx_exact_cell $name]
 set_property IS_LOC_FIXED false $cell
 set_property IS_BEL_FIXED false $cell
 unplace_cell $cell
}
set pairs {}
foreach row $native_rows {
 lassign $row name site bel
 lappend pairs $name "$site/$bel"
}
# Keep each symbol LUT near its nine boundary FFs and the fixed serializers.
# Use wholly empty LUT6/LUT5 pairs, so no unrelated LUT/carry chain is evicted.
set symbol_slots {}
foreach site {SLICE_X115Y110 SLICE_X115Y111 SLICE_X115Y109 SLICE_X115Y112 SLICE_X114Y110 SLICE_X114Y111} {
 foreach letter {A B C D E F G H} {
  set full [get_bels "$site/${letter}6LUT"]
  set half [get_bels "$site/${letter}5LUT"]
  if {[llength [get_cells -quiet -of_objects [concat $full $half]]] == 0} {
   lappend symbol_slots "$site/${letter}6LUT"
  }
 }
}
if {[llength $symbol_slots] < 5} {error "Insufficient empty LUT sites near the TX boundary"}
foreach name [lsort $symbol_names] slot [lrange $symbol_slots 0 4] {
 lappend pairs $name $slot
}
place_cell $pairs
set fixed_native_names $symbol_names
foreach row $native_rows {lappend fixed_native_names [lindex $row 0]}
foreach name $fixed_native_names {
 set cell [tx_exact_cell $name]
 set_property IS_LOC_FIXED true $cell
 set_property IS_BEL_FIXED true $cell
}
foreach row [concat $near_rows [lsort -unique $displaced_rows]] {
 lassign $row name old_site old_bel
 if {![regexp {SLICE_X([0-9]+)Y([0-9]+)} $old_site -> old_x old_y]} {error "Unexpected FF site $old_site"}
 set placed false
 for {set distance 1} {$distance <= 20 && !$placed} {incr distance} {
  for {set dx [expr {-$distance}]} {$dx <= $distance && !$placed} {incr dx} {
   set dy_abs [expr {$distance-abs($dx)}]
   foreach dy [lsort -unique [list $dy_abs [expr {-$dy_abs}]]] {
    set site_name "SLICE_X[expr {$old_x+$dx}]Y[expr {$old_y+$dy}]"
    set site [get_sites -quiet $site_name]
    if {[llength $site] != 1} {continue}
    if {[llength [get_cells -quiet -of_objects [get_bels -of_objects $site]]] != 0} {continue}
    if {[llength [get_bels -quiet "$site_name/$old_bel"]] != 1} {continue}
    place_cell [list $name "$site_name/$old_bel"]
    puts "TX_ECO_DISPLACED_FF [list $name $old_site $site_name $old_bel]"
    set placed true
    break
   }
  }
 }
 if {!$placed} {error "No empty nearby slice for displaced FF $name"}
}
foreach name $target_names {
 set cell [tx_exact_cell $name]
 set_property IS_LOC_FIXED true $cell
 set_property IS_BEL_FIXED true $cell
}
foreach name $move_names {
 if {[get_property LOC [tx_exact_cell $name]] eq ""} {error "Unplaced ECO cell $name"}
}
foreach row $native_rows {
 lassign $row name site bel
 set cell [tx_exact_cell $name]
 if {[get_property LOC $cell] ne $site || [lindex [split [get_property BEL $cell] .] end] ne $bel} {
  error "TX boundary placement differs from frozen golden row: $name"
 }
}
if {[llength [get_cells -hier]] != $before_cells || [llength [get_nets -hier]] != $before_nets} {
 error "Placement ECO changed logical cell/net counts"
}
set f [open native_tx_placement_verification.json w]
puts $f [format {{"passed":true,"native_registers":19,"pad_registers":9,"source_and_reset_registers":10,"native_symbol_luts":5,"relocated_cells":%d,"cells_before":%d,"cells_after":%d,"nets_before":%d,"nets_after":%d,"logical_change":false}} [llength $move_names] $before_cells [llength [get_cells -hier]] $before_nets [llength [get_nets -hier]]]
close $f
puts "PASS_NATIVE_TX_PLACEMENT_ECO registers=[llength $move_names] logical_cells=$before_cells logical_nets=$before_nets"
