# Read-only audit of the routed diagnostic mailbox. No constraint mutations.
set root [file normalize [lindex $argv 0]]
cd $root
set_param general.maxThreads 8
open_checkpoint routed.dcp
proc rx_one {objects label} {
 if {[llength $objects]!=1} {error "$label: expected one object, got $objects"}
 return [lindex $objects 0]
}
proc rx_cell_name {pattern label} {
 return [get_property NAME [rx_one [get_cells -quiet $pattern] $label]]
}
proc rx_pin_name {cell ref} {
 return [get_property NAME [rx_one [get_pins -of_objects [get_cells $cell] -filter "REF_PIN_NAME == $ref"] "$cell/$ref"]]
}
proc rx_save {filename value} {set f [open $filename w];puts $f $value;close $f}
rx_save audit_rx_diagnostics_verification.json {{"passed":false,"state":"RX diagnostic audit running"}}
set h core/rxDiagnostics/mailbox
set held [get_property NAME [get_cells -quiet "$h/held_reg*"]]
set captured [get_property NAME [get_cells -quiet "$h/captured_reg*"]]
if {![llength $held] || ![llength $captured]} {error "Missing diagnostic mailbox register banks"}
set starts {};set ends {};set mapping [dict create]
foreach cell $held {lappend starts [rx_pin_name $cell C]}
foreach cell $captured {
 set d [rx_pin_name $cell D]
 set drivers [get_property NAME [all_fanin -flat -startpoints_only -to [get_pins $d]]]
 if {[llength $drivers]!=1 || [lsearch -exact $starts [lindex $drivers 0]]<0} {
  error "Capture D is not driven directly by a held source bit: $d <- $drivers"
 }
 lappend ends $d
 dict set mapping $d [lindex $drivers 0]
}
set payload_worst 1.0e30
set payload_paths [get_timing_paths -from [get_pins $starts] -to [get_pins $ends] -delay_type max -max_paths [llength $ends] -nworst 1]
set covered [dict create]
foreach path $payload_paths {
 set endpoint [get_property NAME [get_pins [get_property ENDPOINT_PIN $path]]]
 set startpoint [get_property NAME [get_pins [get_property STARTPOINT_PIN $path]]]
 set requirement [get_property REQUIREMENT $path]
 set slack [get_property SLACK $path]
 if {![dict exists $mapping $endpoint] || [dict get $mapping $endpoint] ne $startpoint} {
  error "Unexpected payload timing mapping: $startpoint -> $endpoint"
 }
 if {abs($requirement-8.0)>0.0001 || $slack<0} {error "Payload bit is not bounded by a met 8 ns path: $endpoint"}
 dict set covered $endpoint 1
 set payload_worst [expr {min($payload_worst,$slack)}]
}
if {[dict size $covered]!=[llength $ends]} {error "Missing timed diagnostic payload bits: [dict size $covered]/[llength $ends]"}
report_timing -from [get_pins $starts] -to [get_pins $ends] -delay_type max -max_paths [llength $ends] -nworst 1 -file audit_rx_diagnostics_payload.rpt
set exceptions [report_exceptions -from [get_pins $starts] -to [get_pins $ends] -coverage -return_string]
rx_save audit_rx_diagnostics_exceptions.rpt $exceptions
if {![regexp {Max Delay Datapath Only\s+max_dpo=8(?:\.0+)?\s+-} $exceptions] || [regexp {False Path} $exceptions]} {
 error "Diagnostic payload is missing its 8ns datapath-only constraint or is false-pathed"
}

set toggle_count 0;set first_worst 1.0e30;set setup_worst 1.0e30;set hold_worst 1.0e30
foreach pair {{requestToggle requestSync} {ackToggle ackSync}} {
 lassign $pair launch sync
 set source [rx_cell_name "$h/${launch}_reg" $launch]
 set first [rx_cell_name "$h/$sync/stages_reg?0?" "$sync first"]
 set second [rx_cell_name "$h/$sync/stages_reg?1?" "$sync second"]
 foreach stage [list $first $second] {
  if {![string is true -strict [get_property ASYNC_REG [get_cells $stage]]]} {error "Missing ASYNC_REG: $stage"}
 }
 set source_c [rx_pin_name $source C];set first_c [rx_pin_name $first C]
 set first_d [rx_pin_name $first D];set first_q [rx_pin_name $first Q]
 set second_c [rx_pin_name $second C];set second_d [rx_pin_name $second D]
 set loads [get_property NAME [all_fanout -flat -endpoints_only -from [get_pins $first_q]]]
 if {[llength $loads]!=1 || [lindex $loads 0] ne $second_d} {error "First toggle stage has extra data fanout: $first"}
 set first_clock [get_property NAME [get_clocks -of_objects [get_pins $first_c]]]
 set second_clock [get_property NAME [get_clocks -of_objects [get_pins $second_c]]]
 if {$first_clock ne $second_clock || abs([get_property PERIOD [get_clocks $first_clock]]-8.0)>0.0001} {
  error "Toggle synchronizer does not share a normal 125MHz clock: $sync"
 }
 set p [rx_one [get_timing_paths -from [get_pins $source_c] -to [get_pins $first_d] -delay_type max -max_paths 1] "$sync first-stage path"]
 set req [get_property REQUIREMENT $p];set slack [get_property SLACK $p]
 if {abs($req-8.0)>0.0001 || $slack<0} {error "Unbounded toggle first-stage path: $sync"}
 set first_worst [expr {min($first_worst,$slack)}]
 set p [rx_one [get_timing_paths -from [get_pins $first_c] -to [get_pins $second_d] -delay_type max -max_paths 1] "$sync normal setup"]
 set req [get_property REQUIREMENT $p];set slack [get_property SLACK $p]
 if {abs($req-8.0)>0.0001 || $slack<0} {error "Toggle second-stage setup is not normal 8ns timing: $sync"}
 set setup_worst [expr {min($setup_worst,$slack)}]
 set p [rx_one [get_timing_paths -from [get_pins $first_c] -to [get_pins $second_d] -delay_type min -max_paths 1] "$sync normal hold"]
 set slack [get_property SLACK $p]
 if {$slack<0} {error "Toggle second-stage hold failed: $sync"}
 set hold_worst [expr {min($hold_worst,$slack)}]
 report_timing -from [get_pins $first_c] -to [get_pins $second_d] -delay_type min_max -max_paths 2 -file "audit_rx_diagnostics_${sync}.rpt"
 incr toggle_count
}

# The only new critical fanout warnings are cold-reset PRE pins. Verify their
# actual source and the complete three-stage ASYNC_REG release chains.
set reset_count 0
foreach release {core/rxDiagnostics/sourceRelease core/rxDiagnostics/mailbox/sourceRelease} {
 set stages [get_property NAME [get_cells -quiet "$release/release_0_reg*"]]
 if {[llength $stages]!=3} {error "Cold-reset release is not three stages: $release"}
 foreach stage $stages {
  if {![string is true -strict [get_property ASYNC_REG [get_cells $stage]]]} {error "Cold-reset stage lacks ASYNC_REG: $stage"}
 }
 set first [rx_cell_name "$release/release_0_reg?0?" "$release first"]
 set pre [rx_pin_name $first PRE]
 set drivers [get_property NAME [all_fanin -flat -startpoints_only -to [get_pins $pre]]]
 if {[llength $drivers]!=1 || [lindex $drivers 0] ne "phy_released_reg/C"} {error "Unexpected cold-reset source: $pre <- $drivers"}
 incr reset_count
}
set data [format {{"passed":true,"held_bits":%d,"captured_bits":%d,"mapped_bits":%d,"timed_payload_bits":%d,"payload_requirement_ns":8.0,"payload_worst_slack_ns":%.6f,"toggle_count":%d,"toggle_first_worst_slack_ns":%.6f,"toggle_setup_worst_slack_ns":%.6f,"toggle_hold_worst_slack_ns":%.6f,"reviewed_reset_fanout_count":%d}} \
 [llength $held] [llength $captured] [dict size $mapping] [dict size $covered] $payload_worst $toggle_count $first_worst $setup_worst $hold_worst $reset_count]
rx_save audit_rx_diagnostics_verification.json $data
puts "PASS_RX_DIAGNOSTIC_PHYSICAL payload_bits=[dict size $covered] payload_slack=$payload_worst toggle_setup=$setup_worst toggle_hold=$hold_worst reset_fanout=$reset_count"
