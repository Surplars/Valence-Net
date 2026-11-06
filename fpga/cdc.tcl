# Preserve the upstream's scoped CDC recipes and valid leaf clock startpoints.
source [file join [file dirname [info script]] cdc_constraints.tcl]
source [file join [file dirname [info script]] native_quarter_clock_constraints.tcl]
valence_quarter_tx_clocks tx_clocks
set lanes [valence_quarter_tx_audit u_rgmii]
set forwardPin [get_pins u_rgmii/tx_clock_ddr/CLK]
create_generated_clock -name phy_tx_capture -source $forwardPin -edges {2 4 6} [get_ports eth_txc]
set padClock [get_clocks -of_objects $forwardPin]
foreach c $lanes {
 set oq [get_pins -of_objects $c -filter {REF_PIN_NAME == OQ}]
 # Structural audit proves exact D0=D4. The board oracle checks actual symbols.
 # Only impossible falling-edge PAD transitions are excluded, never D/RST paths.
 set_false_path -fall_from $padClock -through $oq -to [get_ports {eth_txd[*] eth_tx_ctl}]
}
# Match the upstream's physical clock region/root contracts at the RGMII bank.
set region [get_clock_regions -of_objects [get_sites -of_objects [get_package_pins AA7]]]
set mmcms [get_sites -of_objects $region -filter {NAME =~ MMCM*}]
set plls [lsort -dictionary [get_sites -of_objects $region -filter {NAME =~ PLL_X*}]]
if {[llength $region]!=1 || [llength $mmcms]!=1 || [llength $plls]!=2} {error "Missing RGMII clock resources"}
set_property LOC $mmcms [get_cells rx_clocks/mmcm]
set_property LOC [lindex $plls 0] [get_cells tx_clocks/pll]
set_property PHASESHIFT_MODE WAVEFORM [get_cells rx_clocks/mmcm]
set_property CLOCK_DEDICATED_ROUTE SAME_CMT_COLUMN [get_nets -of_objects [get_pins ui_buffer/O]]
foreach path {tx_clocks/raw_div tx_clocks/quarter_div tx_clocks/delay_buffer rx_clocks/feedback_buffer rx_buffer} {
 set c [get_cells $path]
 set n [get_nets -of_objects [get_pins $path/O]]
 set_property CLOCK_REGION $region $c
 set_property USER_CLOCK_ROOT $region $n
}
set_property CLOCK_DELAY_GROUP IPERF_RX [get_nets -of_objects [get_pins {rx_clocks/feedback_buffer/O rx_buffer/O}]]
foreach edge {rise fall} {
 if {$edge eq "rise"} {set extra {}} else {set extra {-clock_fall -add_delay}}
 set_output_delay -clock phy_tx_capture {*}$extra -max 1.250 [get_ports {eth_txd[*] eth_tx_ctl}]
 set_output_delay -clock phy_tx_capture {*}$extra -min -1.250 [get_ports {eth_txd[*] eth_tx_ctl}]
}
set async_cells [get_cells -hier -filter {ASYNC_REG == TRUE}]
set async_controls [get_pins -of_objects $async_cells -filter {REF_PIN_NAME == PRE || REF_PIN_NAME == CLR}]
if {[llength $async_controls]} {set_false_path -to $async_controls}
# The MSB may be merged with a binary pointer: trace each actual capture driver.
valence_stream_cdc_constraints core/crossing/fifo 8.0
set link_cells [get_cells -quiet {core/rxLink/stages_reg[0]}]
set link_ends [get_pins -of_objects $link_cells -filter {REF_PIN_NAME == D}]
set link_drivers [all_fanin -flat -startpoints_only -to $link_ends]
set link_starts [get_pins -of_objects [get_cells -of_objects $link_drivers] -filter {REF_PIN_NAME == C}]
if {[llength $link_starts]!=1 || [llength $link_ends]!=1} {error "Missing link crossing: starts=$link_starts ends=$link_ends"}
puts "IPERF_LINK_BOUNDED starts=$link_starts ends=$link_ends"
set_max_delay -datapath_only 8.0 -from $link_starts -to $link_ends
# Distributed dual-clock payload RAM: only write-clock -> captured payload arcs.
# Constant payload fields can be removed; enumerate the surviving destination FFs.
set captures [get_cells -quiet {core/crossing/fifo/output_0_reg*}]
if {![llength $captures]} {error "Missing FIFO payload capture registers"}
set ends [get_pins -of_objects $captures -filter {REF_PIN_NAME == D}]
set starts {}
foreach pin [all_fanin -flat -startpoints_only -to $ends] {
 set cell [get_cells -of_objects $pin]
 set name [get_property NAME $cell]
 set ref [get_property REF_NAME $cell]
 set rp [get_property REF_PIN_NAME $pin]
 if {[string first "core/crossing/fifo/storage_ext/" $name]==0 &&
     $ref in {RAMD32 RAMS32 RAMD64E RAMS64E} && $rp in {CLK WCLK}} {
  lappend starts $pin
 }
}
if {![llength $starts]} {error "Missing FIFO RAM leaf clock startpoints"}
set_max_delay -datapath_only 8.0 -from $starts -to $ends
set_bus_skew 8.0 -from $starts -to $ends
puts "IPERF_FIFO_PAYLOAD_BOUNDED starts=[llength $starts] captures=[llength $ends]"
set_max_delay -datapath_only 10.0 -from [get_ports eth_mdio] -to [get_pins mdio_meta_reg/D]
# Clause22 serializer changes on MDC falling edges, samples on rising edges;
# each 20ns pad leg is bounded against the >=200ns half-period, as upstream.
foreach port {eth_mdc eth_mdio eth_reset_gate led uart_tx} {
 set endpoint [get_ports $port]
 set starts [filter [all_fanin -flat -startpoints_only -to $endpoint] {REF_PIN_NAME == C || REF_PIN_NAME == CLK}]
 if {![llength $starts]} {error "Missing management register: $port"}
 set_max_delay -datapath_only 20.0 -from $starts -to $endpoint
}

# UART only observes RX lock; do not feed its synchronizer back into network reset.
set uart_lock_first [get_cells -quiet logger/rx_lock_meta_reg]
set uart_lock_end [get_pins -of_objects $uart_lock_first -filter {REF_PIN_NAME == D}]
if {[llength $uart_lock_end]!=1} {error "Missing UART RX-lock synchronizer"}
# MMCM LOCKED is asynchronous status without a data launch clock. Only this
# first-stage D is excepted; meta Q -> second-stage D remains fully timed.
set_false_path -to $uart_lock_end

# IPERF_RX_DIAGNOSTICS: held atomic snapshot + two handshake toggles.
# Bound only the real mailbox crossings; both synchronizer second stages stay timed.
set diag_hier core/rxDiagnostics/mailbox
valence_cdc_bus [get_cells -quiet "$diag_hier/held_reg*"] \
    [get_cells -quiet "$diag_hier/captured_reg*"] 8.0 diagnostic_snapshot_payload
foreach pair {{requestToggle requestSync} {ackToggle ackSync}} {
 lassign $pair toggle synchronizer
 valence_cdc_bus [get_cells -quiet "$diag_hier/${toggle}_reg*"] \
     [get_cells -quiet "$diag_hier/$synchronizer/stages_reg?0?"] 8.0 diagnostic_$toggle
}
