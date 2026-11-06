set run [file normalize [lindex $argv 0]]
set output [file normalize [lindex $argv 1]]
set_param general.maxThreads 4
proc capture_physical_cells {} {
 set cells [get_cells -hier -filter {IS_PRIMITIVE == 1}]
 set names [get_property NAME $cells]
 set refs [get_property REF_NAME $cells]
 set locations [get_property LOC $cells]
 set bels [get_property BEL $cells]
 set count [llength $names]
 if {[llength $refs]!=$count || [llength $locations]!=$count || [llength $bels]!=$count} {
  error "Cell property vectors have different lengths"
 }
 set snapshot [dict create]
 foreach name $names ref $refs loc $locations bel $bels {
  dict set snapshot $name [list $ref $loc $bel]
 }
 return $snapshot
}
open_checkpoint [file join $run routed.dcp]
set current [capture_physical_cells]
close_design
open_checkpoint [file join $run imported_routed.dcp]
set baseline [capture_physical_cells]
if {[dict size $baseline] != [dict size $current]} {error "Primitive count changed"}
set f [open $output w]
set moved 0
dict for {name before} $baseline {
 if {![dict exists $current $name]} {error "Primitive disappeared: $name"}
 set after [dict get $current $name]
 if {[lindex $before 0] ne [lindex $after 0]} {error "Primitive type changed: $name"}
 if {$before ne $after} {
  puts $f [join [concat [list $name] $before $after] "\t"]
  incr moved
 }
}
close $f
set f [open "$output.json" w]
puts $f [format {{"passed":true,"primitive_count":%d,"primitive_names_and_types_unchanged":true,"changed_locations_or_bels":%d,"formal_logic_equivalence_claim":false}} [dict size $current] $moved]
close $f
puts "PASS_NATIVE_PHYSICAL_SCOPE primitives=[dict size $current] changed_locations_or_bels=$moved"
