# Read-only wrapper: open the routed checkpoint once via the frozen base audit,
# then validate UART-specific synchronizer/exception/output paths on that design.
# Usage: vivado -mode batch -source audit_uart.tcl -tclargs <frozen-run-directory>
set uart_audit_directory [file dirname [file normalize [info script]]]
source [file join $uart_audit_directory audit.tcl]

proc uart_require_one {objects description} {
    if {[llength $objects] != 1} {error "$description: expected 1 object, found [llength $objects]: $objects"}
    return [lindex $objects 0]
}
proc uart_named_one {objects description} {
    # Preserve plain names instead of transient Vivado collection handles.
    return [format "%s" [get_property NAME [uart_require_one $objects $description]]]
}
proc uart_number {value description} {
    if {![string is double -strict $value]} {error "$description is not numeric: $value"}
    return [expr {double($value)}]
}
proc uart_json_string {value} {
    return "\"[string map [list "\\" "\\\\" "\"" "\\\"" "\n" "\\n" "\r" "\\r" "\t" "\\t"] $value]\""
}
proc uart_save {filename contents} {
    set channel [open $filename w]
    puts $channel $contents
    close $channel
}
# Invalidate an earlier result before any UART-specific queries; a failed
# rerun must never leave a previous pass as the latest audit result.
uart_save audit_uart_verification.json {{"passed":false,"state":"UART audit running"}}

set uart_meta [uart_named_one [get_cells -quiet logger/rx_lock_meta_reg] "UART first stage"]
set uart_sync [uart_named_one [get_cells -quiet logger/rx_lock_sync_reg] "UART second stage"]
set uart_meta_async [get_property ASYNC_REG [get_cells $uart_meta]]
set uart_sync_async [get_property ASYNC_REG [get_cells $uart_sync]]
if {![string is true -strict $uart_meta_async] || ![string is true -strict $uart_sync_async]} {
    error "UART RX-lock stages must both retain ASYNC_REG: $uart_meta_async / $uart_sync_async"
}
set uart_meta_d [uart_named_one [get_pins -of_objects [get_cells $uart_meta] -filter {REF_PIN_NAME == D}] "UART meta D"]
set uart_meta_q [uart_named_one [get_pins -of_objects [get_cells $uart_meta] -filter {REF_PIN_NAME == Q}] "UART meta Q"]
set uart_meta_c [uart_named_one [get_pins -of_objects [get_cells $uart_meta] -filter {REF_PIN_NAME == C || REF_PIN_NAME == CLK}] "UART meta clock"]
set uart_sync_d [uart_named_one [get_pins -of_objects [get_cells $uart_sync] -filter {REF_PIN_NAME == D}] "UART sync D"]
set uart_sync_c [uart_named_one [get_pins -of_objects [get_cells $uart_sync] -filter {REF_PIN_NAME == C || REF_PIN_NAME == CLK}] "UART sync clock"]
if {$uart_meta ne "logger/rx_lock_meta_reg" || $uart_sync ne "logger/rx_lock_sync_reg" ||
    $uart_meta_d ne "logger/rx_lock_meta_reg/D" || $uart_sync_d ne "logger/rx_lock_sync_reg/D"} {
    error "UART identity mismatch: meta=$uart_meta D=$uart_meta_d sync=$uart_sync D=$uart_sync_d"
}
puts "UART_IDENTITIES meta=$uart_meta D=$uart_meta_d sync=$uart_sync D=$uart_sync_d"
set uart_meta_clock [uart_named_one [get_clocks -of_objects [get_pins $uart_meta_c]] "UART meta timing clock"]
set uart_sync_clock [uart_named_one [get_clocks -of_objects [get_pins $uart_sync_c]] "UART sync timing clock"]
if {$uart_meta_clock ne $uart_sync_clock} {
    error "UART synchronizer stages do not share one timing clock"
}
set uart_clock_period [uart_number [get_property PERIOD [get_clocks $uart_meta_clock]] "UART clock period"]
if {abs($uart_clock_period - 8.0) > 0.0001} {error "UART serializer is not clocked at 125 MHz"}

# The first-stage output may feed only the second-stage data input.
set uart_stage_loads [all_fanout -flat -endpoints_only -from [get_pins $uart_meta_q]]
if {[llength $uart_stage_loads] != 1 ||
    [get_property NAME [lindex $uart_stage_loads 0]] ne $uart_sync_d} {
    error "Unexpected first-stage fanout: $uart_stage_loads"
}
set uart_sync_drivers [all_fanin -flat -startpoints_only -to [get_pins $uart_sync_d]]
if {[llength $uart_sync_drivers] != 1 ||
    [get_property NAME [lindex $uart_sync_drivers 0]] ne $uart_meta_c} {
    error "UART second stage is not directly driven by the first stage: $uart_sync_drivers"
}

# Coverage alone cannot prove endpoint scope, particularly for an asynchronous
# MMCM status output with no ordinary clocked timing startpoint.
write_xdc -force -type timing audit_uart_restored.xdc
set uart_xdc_channel [open audit_uart_restored.xdc r]
set uart_restored_xdc [read $uart_xdc_channel]
close $uart_xdc_channel
set uart_first_scope_rows 0
foreach uart_line [split $uart_restored_xdc "\n"] {
    if {[string match "*rx_lock_meta_reg*" $uart_line]} {
        puts "UART_RESTORED_FIRST_STAGE $uart_line"
        if {![regexp {^\s*set_false_path\s+-to\s+(\[get_pins\s+(?:\{logger/rx_lock_meta_reg/D\}|"logger/rx_lock_meta_reg/D"|logger/rx_lock_meta_reg/D|-of_objects\s+\[get_cells\s+-quiet\s+logger/rx_lock_meta_reg\]\s+-filter\s+\{REF_PIN_NAME\s*==\s*D\})\])\s*$} $uart_line -> uart_endpoint_query]} {
            error "Unexpected restored first-stage constraint: $uart_line"
        }
        # Only the safelisted get_pins/get_cells query above is evaluated.
        set uart_restored_endpoint [uart_named_one [get_pins [subst $uart_endpoint_query]] "Restored UART false-path endpoint"]
        if {$uart_restored_endpoint ne $uart_meta_d} {
            error "Restored false path resolves to $uart_restored_endpoint instead of $uart_meta_d"
        }
        incr uart_first_scope_rows
    }
}
if {$uart_first_scope_rows != 1} {error "Restored XDC lacks exactly one endpoint-only meta/D false path"}
set uart_lock_pin [uart_named_one [get_pins -quiet rx_clocks/mmcm/LOCKED] "RX MMCM LOCKED pin"]
set uart_lock_nets [get_property NAME [get_nets -segments -of_objects [get_pins $uart_lock_pin]]]
set uart_meta_nets [get_property NAME [get_nets -segments -of_objects [get_pins $uart_meta_d]]]
set uart_lock_direct 0
foreach uart_net $uart_lock_nets {
    if {[lsearch -exact $uart_meta_nets $uart_net] >= 0} {set uart_lock_direct 1}
}
if {!$uart_lock_direct} {error "RX MMCM LOCKED is not directly connected to UART meta/D"}
set uart_first_exceptions [report_exceptions -to [get_pins $uart_meta_d] -coverage -return_string]
uart_save audit_uart_first_stage_exceptions.rpt $uart_first_exceptions
set uart_first_false_rows 0
set uart_first_coverage -1
foreach uart_line [split $uart_first_exceptions "\n"] {
    if {[regexp {^\s*[0-9]+\s+False Path\s+false\s+false\s+1 pins?\s+1\s+([0-9]+(?:\.[0-9]+)?)\s*$} $uart_line -> uart_coverage]} {
        set uart_first_coverage [uart_number $uart_coverage "UART first-stage coverage"]
        incr uart_first_false_rows
    }
}
if {$uart_first_false_rows != 1 || $uart_first_coverage < 0 || $uart_first_coverage > 100} {
    error "First-stage D must have exactly one endpoint-only false-path report row; see audit_uart_first_stage_exceptions.rpt"
}
set uart_stage_exceptions [report_exceptions -from [get_pins $uart_meta_c] -to [get_pins $uart_sync_d] -coverage -return_string]
uart_save audit_uart_between_stage_exceptions.rpt $uart_stage_exceptions
if {[regexp {False Path|Max Delay|Min Delay|Multicycle} $uart_stage_exceptions]} {
    error "UART meta-to-sync path unexpectedly has a timing exception"
}

# Setup and hold must both exist and remain positive. Finding no path is a
# failure, since a broad false path could otherwise masquerade as clean timing.
set uart_stage_setup [uart_require_one [get_timing_paths -from [get_pins $uart_meta_c] -to [get_pins $uart_sync_d] -delay_type max -max_paths 1] "UART meta-to-sync setup path"]
set uart_stage_setup_slack [uart_number [get_property SLACK $uart_stage_setup] "UART synchronizer setup slack"]
set uart_stage_setup_requirement [uart_number [get_property REQUIREMENT $uart_stage_setup] "UART synchronizer setup requirement"]
set uart_stage_hold [uart_require_one [get_timing_paths -from [get_pins $uart_meta_c] -to [get_pins $uart_sync_d] -delay_type min -max_paths 1] "UART meta-to-sync hold path"]
set uart_stage_hold_slack [uart_number [get_property SLACK $uart_stage_hold] "UART synchronizer hold slack"]
if {$uart_stage_setup_slack <= 0 || $uart_stage_hold_slack <= 0} {error "UART synchronizer setup/hold is not positive"}
if {abs($uart_stage_setup_requirement - $uart_clock_period) > 0.0001} {
    error "UART synchronizer is not checked against one normal TX-clock period"
}
report_timing -from [get_pins $uart_meta_c] -to [get_pins $uart_sync_d] -delay_type min_max -max_paths 2 -path_type full_clock_expanded -file audit_uart_sync.rpt

# Re-discover real sequential clock-pin startpoints at the UART pad. Every
# surviving startpoint must have a timed path under the 20 ns datapath bound.
set uart_port [uart_named_one [get_ports -quiet uart_tx] "UART output port"]
set uart_pad_drivers [filter [all_fanin -flat -startpoints_only -to [get_ports $uart_port]] {REF_PIN_NAME == C || REF_PIN_NAME == CLK}]
if {[llength $uart_pad_drivers] == 0} {error "UART output has no sequential clock-pin startpoint"}
set uart_pad_drivers [get_property NAME $uart_pad_drivers]
set uart_output_json {}
set uart_output_worst_slack 1.0e30
foreach uart_start $uart_pad_drivers {
    set uart_out_exception [report_exceptions -from [get_pins $uart_start] -to [get_ports $uart_port] -coverage -return_string]
    if {![regexp {Max Delay Datapath Only\s+max_dpo=20(?:\.0+)?\s+-} $uart_out_exception]} {
        error "Missing 20 ns datapath-only UART output exception from $uart_start"
    }
    if {[regexp {False Path} $uart_out_exception]} {error "UART output path is false-pathed from $uart_start"}
    set uart_path [uart_require_one [get_timing_paths -from [get_pins $uart_start] -to [get_ports $uart_port] -delay_type max -max_paths 1] "UART output path from $uart_start"]
    set uart_requirement [uart_number [get_property REQUIREMENT $uart_path] "UART output requirement"]
    set uart_slack [uart_number [get_property SLACK $uart_path] "UART output slack"]
    if {abs($uart_requirement - 20.0) > 0.0001 || $uart_slack < 0} {
        error "UART output path does not meet the 20 ns budget from $uart_start"
    }
    set uart_output_worst_slack [expr {min($uart_output_worst_slack,$uart_slack)}]
    lappend uart_output_json [format {{"startpoint":%s,"requirement_ns":%.6f,"slack_ns":%.6f}} \
        [uart_json_string $uart_start] $uart_requirement $uart_slack]
}
report_timing -from [get_pins $uart_pad_drivers] -to [get_ports $uart_port] -delay_type max -max_paths [llength $uart_pad_drivers] -path_type full_clock_expanded -file audit_uart_output.rpt
report_exceptions -from [get_pins $uart_pad_drivers] -to [get_ports $uart_port] -coverage -file audit_uart_output_exceptions.rpt

set uart_json [format {
{
  "passed":true,
  "checked_utc":%s,
  "checkpoint":%s,
  "clock":%s,
  "clock_period_ns":%.6f,
  "synchronizer_stage_count":%d,
  "meta_cell":%s,
  "sync_cell":%s,
  "meta_async_reg":%s,
  "sync_async_reg":%s,
  "first_stage_exception_pin":%s,
  "first_stage_scoped_false_path_rows":%d,
  "first_stage_to_coverage_percent":%.6f,
  "first_stage_restored_xdc_rows":%d,
  "first_stage_direct_mmcm_locked":true,
  "meta_to_sync_setup_requirement_ns":%.6f,
  "meta_to_sync_setup_slack_ns":%.6f,
  "meta_to_sync_hold_slack_ns":%.6f,
  "uart_output_startpoint_count":%d,
  "uart_output_worst_slack_ns":%.6f,
  "uart_output_paths":[%s]
}
} [uart_json_string [clock format [clock seconds] -gmt 1 -format {%Y-%m-%dT%H:%M:%SZ}]] \
    [uart_json_string [file normalize routed.dcp]] \
    [uart_json_string $uart_meta_clock] $uart_clock_period \
    [llength [list $uart_meta $uart_sync]] \
    [uart_json_string $uart_meta] [uart_json_string $uart_sync] \
    [expr {[string is true -strict $uart_meta_async] ? "true" : "false"}] \
    [expr {[string is true -strict $uart_sync_async] ? "true" : "false"}] \
    [uart_json_string $uart_meta_d] $uart_first_false_rows $uart_first_coverage $uart_first_scope_rows \
    $uart_stage_setup_requirement $uart_stage_setup_slack $uart_stage_hold_slack \
    [llength $uart_pad_drivers] $uart_output_worst_slack [join $uart_output_json ","]]
uart_save audit_uart_verification.json $uart_json
puts "PASS_UART_PHYSICAL stages=[llength [list $uart_meta $uart_sync]] first_stage_false_paths=$uart_first_false_rows setup_ns=$uart_stage_setup_slack hold_ns=$uart_stage_hold_slack output_starts=[llength $uart_pad_drivers] output_slack_ns=$uart_output_worst_slack"
