#!/usr/bin/env python3
"""Seal a completed, audited Vivado run; never interpret simulation as board speed."""
import argparse,datetime,hashlib,json,pathlib,re,shutil
ROOT=pathlib.Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser();ap.add_argument("run",type=pathlib.Path);a=ap.parse_args()
    run=a.run.resolve()
    manifest=json.loads((run/"provenance.json").read_text())
    for rel,digest in manifest["input_sha256"].items():
        if sha(run/rel)!=digest:raise RuntimeError("Frozen input changed: "+rel)
    for rel,digest in manifest["source_sha256"].items():
        if rel.startswith("src/") or rel in ["build.mill","fpga/iperf_board.sv","fpga/iperf_uart_log.sv","fpga/cdc.tcl","fpga/board.xdc","fpga/native_gmac_clocks.sv"]:
            if sha(ROOT/rel)!=digest:raise RuntimeError("Current hardware differs: "+rel)
    placement_retry=manifest.get("implementation_retry",{}).get("native_tx_placement",False)
    placement_path=run/"native_tx_placement_verification.json"
    if placement_retry:
        placement=json.loads(placement_path.read_text())
        if (not placement["passed"] or placement["native_registers"]!=19 or
            placement["native_symbol_luts"]!=5 or not 24<=placement["relocated_cells"]<=56 or
            placement["logical_change"] or placement["cells_before"]!=placement["cells_after"] or
            placement["nets_before"]!=placement["nets_after"]):
            raise RuntimeError("Native TX placement ECO changed more than its reviewed physical scope")
        if manifest["implementation_retry"]["checkpoint_sha256"]!=manifest["input_sha256"]["imported_routed.dcp"]:
            raise RuntimeError("Placement ECO imported checkpoint provenance differs")
        scope_path=run/"native-physical-scope.json"
        scope=json.loads(scope_path.read_text())
        if (not scope["passed"] or not scope["primitive_names_and_types_unchanged"] or
            scope["primitive_count"]!=229683 or scope["changed_locations_or_bels"]!=38 or
            scope["explicitly_adjusted_cells"]!=36 or scope["unexpected_changed_cells"] or
            set(scope["implicit_reset_macro_cells"])!={"tx_sync_reg[0]","tx_sync_reg[1]"}):
            raise RuntimeError("Native physical scope comparison differs from reviewed 38-cell change")
        if (scope["imported_checkpoint_sha256"]!=manifest["input_sha256"]["imported_routed.dcp"] or
            scope["routed_checkpoint_sha256"]!=sha(run/"routed.dcp") or
            scope["audit_script_sha256"]!=sha(run/"audit_native_scope.tcl") or
            scope["diff_sha256"]!=sha(run/"native-physical-diff.tsv")):
            raise RuntimeError("Native scope comparison evidence changed")
        review_path=run/"clock-guidance-review.json"
        review=json.loads(review_path.read_text())
        blob=(run/"console.log").read_bytes()
        console=blob.decode("utf-16") if blob.startswith((b"\xff\xfe",b"\xfe\xff")) else blob.decode("utf-8")
        critical_ids=re.findall(r"CRITICAL WARNING: \[([^]]+)\]",console)
        if (not review["reviewed"] or critical_ids!=["Route 35-4475"] or
            review["implementation_critical_warning_ids"]!=critical_ids or
            review["console_sha256"]!=sha(run/"console.log") or
            review["preserved_clock_routes_sha256"]!=sha(run/"preserved_clock_routes.txt")):
            raise RuntimeError("Unexpected/unreviewed implementation critical warning")
    seed_path=run/"tx_boundary_seed_verification.json"
    seed=json.loads(seed_path.read_text())
    if seed!={"passed":True,"registers":19,"symbol_and_phase_luts":5,
        "applied_before_placement":True,"timing_constraints_relaxed":False}:
        raise RuntimeError("Fresh implementation did not preserve the reviewed TX boundary seed")
    reference=json.loads((run/"tx-boundary-reference.json").read_text())
    if (reference["reference_bit_sha256"]!="493eb325dac8e59082d51301328ad7c79da062f2e17f1c155b50bc44d9108723" or
        reference["register_count"]!=19 or reference["lut_count"]!=5 or
        reference["register_layout_sha256"]!=sha(run/"gold-native-layout.tsv")):
        raise RuntimeError("TX boundary reference is not the verified ACK1 implementation")
    blob=(run/"console.log").read_bytes()
    console=blob.decode("utf-16") if blob.startswith((b"\xff\xfe",b"\xfe\xff")) else blob.decode("utf-8")
    if not placement_retry and re.search(r"CRITICAL WARNING:|^ERROR:",console,re.M):
        raise RuntimeError("Fresh implementation contains an unreviewed critical warning or error")
    paired_child=manifest.get("implementation_retry",{}).get("post_route_qualification",False)
    if paired_child:
        retry=manifest["implementation_retry"]
        parent=pathlib.Path(retry["parent_directory"])
        old=json.loads((parent/"provenance.json").read_text())
        if (retry["parent_input_manifest_sha256"]!=sha(parent/"provenance.json") or
            retry["checkpoint_sha256"]!=manifest["input_sha256"]["imported_routed.dcp"] or
            retry["checkpoint_sha256"]!=sha(parent/retry["selected_parent_stage"]) or
            not retry["rtl_unchanged"] or not retry["timing_constraints_unchanged"]):
            raise RuntimeError("Routed qualification ancestry differs")
        for rel,digest in old["input_sha256"].items():
            hardware=(rel.startswith("rtl/") or rel in ["iperf_board.sv","iperf_uart_log.sv",
                "board.xdc","cdc.tcl","cdc_constraints.tcl","native_rgmii.sv",
                "native_gmac_pins.xdc","native_gmac_clocks.sv","native_gmac_divided_clock.sv",
                "native_quarter_clock_constraints.tcl"])
            if hardware and manifest["input_sha256"].get(rel)!=digest:
                raise RuntimeError("Routed qualification changed RTL/timing constraints: "+rel)
        packing=json.loads((run/"boundary-packing-verification.json").read_text())
        if (not packing["passed"] or packing["fixed_cells"]!=24 or packing["fixed_registers"]!=19 or
            packing["dont_touch_symbol_luts"]!=5 or
            packing["checkpoint_sha256"]!=sha(parent/"opt_seeded.dcp") or
            packing["property_report_sha256"]!=sha(run/"boundary_properties.tsv") or
            reference["packing_verification_sha256"]!=sha(run/"boundary-packing-verification.json")):
            raise RuntimeError("Paired LUT pre-placement evidence differs")
    final_boundary=json.loads((run/"final-boundary-verification.json").read_text())
    if final_boundary!={"passed":True,"fixed_cells":24,"fixed_registers":19,
        "dont_touch_symbol_luts":5,"explicit_paired_lut_packing":True}:
        raise RuntimeError("Final boundary protection audit failed")
    expected={}
    for line in (run/"gold-native-layout.tsv").read_text().splitlines():
        if line.strip():
            name,site,bel=line.split("\t");expected[name]=(site,bel.split(".")[-1],False)
    for name,bel in reference["new_candidate_paired_symbol_lut_bels"].items():
        expected[name]=("SLICE_X115Y110",bel,True)
    seen=set()
    for line in (run/"final-boundary-properties.tsv").read_text().splitlines():
        name,ref,site,bel,loc_fixed,bel_fixed,dont_touch=line.split("\t")
        if name in seen or name not in expected:raise RuntimeError("Unexpected/duplicate final boundary cell")
        seen.add(name);site_expected,bel_expected,is_lut=expected[name]
        if (site!=site_expected or bel.split(".")[-1]!=bel_expected or
            loc_fixed!="1" or bel_fixed!="1" or is_lut and dont_touch!="1"):
            raise RuntimeError("Final paired boundary property differs: "+name)
    if seen!=set(expected) or len(seen)!=24:raise RuntimeError("Incomplete final boundary coverage")
    if (run/"progress.txt").read_text().strip()!="completed bit":raise RuntimeError("Bit build incomplete")
    phase=float((run/"audit_rx_phase.txt").read_text())
    if phase!=manifest["rx_capture_phase_degrees"] or phase!=45.0:
        raise RuntimeError("Routed checkpoint is not the corrected45deg RX capture")
    reuse=manifest.get("resumed_routing")
    if reuse:
        eco=json.loads((run/"rx_phase_eco.json").read_text())
        if (eco["phase_before"]!=reuse["rx_phase_before_deg"] or eco["phase_after"]!=phase
            or eco["cells_before"]!=eco["cells_after"] or eco["nets_before"]!=eco["nets_after"]):
            raise RuntimeError("Phase ECO changed more than the guarded MMCM parameter")
    timing=(run/"audit_timing.rpt").read_text()
    summary=timing.split("| Design Timing Summary",1)[1]
    row=next(x for x in summary.splitlines() if re.match(r"^\s*-?\d+\.\d+\s+-?\d+\.\d+\s+\d+\s+\d+",x))
    nums=list(map(float,row.split()))
    if len(nums)!=12 or any(nums[i]<0 for i in [0,4,8]) or any(nums[i]!=0 for i in [2,6,10]):
        raise RuntimeError("Setup/hold/pulse width timing failed")
    skew=(run/"audit_bus_skew.rpt").read_text()
    if "VIOLATED" in skew or not re.search(r"Slack\s*\(MET\)",skew):
        raise RuntimeError("Bus skew checks did not pass")
    bit=run/"valence_iperf.bit"
    if bit.stat().st_size<1000:raise RuntimeError("Missing/empty bitstream")
    for n in ["audit_check_timing.rpt","audit_cdc.rpt","audit_drc.rpt","route_status.rpt","routed_utilization.rpt"]:
        if not (run/n).is_file():raise RuntimeError("Missing report: "+n)
    checks=(run/"audit_check_timing.rpt").read_text()
    for category in ["no_clock","constant_clock","unconstrained_internal_endpoints",
                     "multiple_clock","generated_clocks","loops","latch_loops"]:
        if "checking "+category+" (0)" not in checks:
            raise RuntimeError("Unresolved timing check: "+category)
    if len(re.findall(r"Slack\s*\(MET\)",skew))!=4:
        raise RuntimeError("Expected both Gray-pointer, FIFO payload, and diagnostic snapshot skew checks")
    cdc=(run/"audit_cdc.rpt").read_text()
    critical_destinations=re.findall(r"^\s*\d+\s+CDC-10\s+Critical.*?\s+(\S+)\s*$",cdc,re.M)
    expected_resets={"core/crossing/fifo/sourceRelease/release_0_reg[0]/PRE",
        "core/rxRelease/release_0_reg[0]/PRE","rx_sync_reg[0]/PRE","phy_sync_reg[0]/PRE"}
    if set(critical_destinations)!=expected_resets or len(critical_destinations)!=4:
        raise RuntimeError("CDC reset structure differs from reviewed four-reset pattern")
    critical_fanout=re.findall(r"^\s*\d+\s+CDC-11\s+Critical.*?\s+(\S+)\s*$",cdc,re.M)
    expected_fanout={"core/rxDiagnostics/mailbox/sourceRelease/release_0_reg[0]/PRE",
                     "core/rxDiagnostics/sourceRelease/release_0_reg[0]/PRE"}
    if set(critical_fanout)!=expected_fanout or len(critical_fanout)!=2:
        raise RuntimeError("Diagnostic cold-reset fanout differs from reviewed reset-only pattern")
    if re.search(r"^\s*\d+\s+CDC-(?!(?:10|11)\b)\d+\s+Critical",cdc,re.M):
        raise RuntimeError("Unexpected critical CDC finding")
    drc=(run/"audit_drc.rpt").read_text()
    if re.search(r"\|\s*(Error|Critical Warning)\s*\|",drc):
        raise RuntimeError("DRC error or critical warning")
    route=(run/"route_status.rpt").read_text()
    route_errors=re.search(r"# of nets with routing errors\.+\s*:\s*(\d+)",route)
    if not route_errors or int(route_errors[1])!=0:
        raise RuntimeError("Routing errors or missing status")
    functional=json.loads((ROOT/"build/functional-verification.json").read_text())
    for rel,digest in functional["source_sha256"].items():
        if sha(ROOT/rel)!=digest:raise RuntimeError("Functional source evidence is stale: "+rel)
    model_board_path=ROOT/"build/gmii-board-equivalence.json"
    model_board=json.loads(model_board_path.read_text())
    if not model_board["passed"] or model_board["different_modules"]!=["GmiiFrameRx"] or model_board["equal_module_count"]!=212:
        raise RuntimeError("Current network FIR comparison failed")
    if sha(ROOT/"scripts/check_model_board.py")!=model_board["script_sha256"] or sha(ROOT/"build/gmii/gmii.fir")!=model_board["model_fir_sha256"] or sha(run/"board.fir")!=model_board["board_fir_sha256"]:
        raise RuntimeError("Model/board FIR comparison changed")
    for name,digest in model_board["frozen_rtl_sha256"].items():
        if sha(run/"rtl"/name)!=digest:raise RuntimeError("Compared board RTL changed: "+name)
    window_path=ROOT/"build/transport-comparison/verification.json"
    window=json.loads(window_path.read_text())
    if (not window["passed"] or window["ack_latency_us"]!=[256,512,640] or
        window["tx_banks"]!=64 or window["rx_capacity_bytes"]!=262144 or len(window["cases"])!=13 or
        any(value<900 for value in window["model_mbps"]["64"].values())):
        raise RuntimeError("Transport comparison or burst/line-rate tests failed")
    if window["ack_counts_512_frames"]["32"]<1000 or window["ack_counts_512_frames"]["64"]>520:
        raise RuntimeError("Redundant ACK reduction did not pass")
    if sha(ROOT/"sim/gmii_burst.cpp")!=window["driver_sha256"] or sha(ROOT/"sim/test_transport.py")!=window["script_sha256"]:
        raise RuntimeError("Transport test source changed")
    for name,digest in window["model_sha256"]["64"].items():
        if sha(ROOT/"build/gmii"/name)!=digest:raise RuntimeError("Current model differs from comparison: "+name)
    for case in window["cases"]:
        logfile=ROOT/"build/transport-comparison"/case["log"]
        if not case["passed"] or sha(logfile)!=case["log_sha256"]:raise RuntimeError("Transport test log changed")
    ab_path=ROOT/"build/rx-window-ab/verification.json"
    ab=json.loads(ab_path.read_text())
    if not ab["passed"] or ab["driver_sha256"]!=sha(ROOT/"sim/gmii_burst.cpp"):
        raise RuntimeError("RX capacity/scale differential proof differs")
    if [(o["capacity_bytes"],o["wire_window_max_bytes"],o["window_scale"],o["ack_packets"],o["payload_bytes"]) for o in ab["observations"]] != [
        (131072,131032,2,513,747520),(262144,262104,3,513,747520)]:
        raise RuntimeError("RX capacity/scale or ACK cadence did not match the independent wire peer")
    for o,label in zip(ab["observations"],["128K","256K"]):
        if sha(ROOT/"build/rx-window-ab"/(label+"-forward.log"))!=o["log_sha256"]:
            raise RuntimeError("RX differential wire log changed")
    if ab["observations"][1]["model_sha256"]!=window["model_sha256"]["64"]:
        raise RuntimeError("Current model differs from RX differential proof")
    transport=manifest.get("transport")
    if transport!={"tx_banks":64,"rx_words":16384,"receive_bytes":262144,"congestion_cap_bytes":131072,
        "local_window_scale":3,"build_tag":"TX64-RX256K-N57B"}:
        raise RuntimeError("Frozen transport parameters differ from tested candidate")
    receive_window_path=ROOT/"build/rxqueue/verification.json"
    receive_window=json.loads(receive_window_path.read_text())
    if not receive_window["passed"] or receive_window["zero_window_rounds"]!=3 or receive_window["partial_reopen_bytes"]!=16:
        raise RuntimeError("RX credit, zero-window, wrap or SACK test did not pass")
    if sha(ROOT/"build/rxqueue/test.log")!=receive_window["test_log_sha256"] or sha(ROOT/"sim/receive_window.cpp")!=receive_window["test_driver_sha256"]:
        raise RuntimeError("RX window test evidence changed")
    board=json.loads((ROOT/"build/board-reset/verification.json").read_text())
    if not board["passed"] or board.get("tx_bytes_checked")!=64 or board.get("rx_bytes_checked")!=64 or board.get("rx_mmcm_phase_degrees")!=phase:
        raise RuntimeError("Missing current board-boundary test")
    for rel,digest in board["input_sha256"].items():
        if rel in ["board_reset_tb.sv","glbl.v"]:continue
        if sha(run/rel)!=digest:raise RuntimeError("Board test differs from run: "+rel)
    uart_physical=json.loads((run/"audit_uart_verification.json").read_text())
    if not uart_physical["passed"] or uart_physical["synchronizer_stage_count"]!=2:
        raise RuntimeError("UART physical audit failed")
    if (uart_physical["first_stage_scoped_false_path_rows"]!=1
        or uart_physical["first_stage_restored_xdc_rows"]!=1
        or not uart_physical["first_stage_direct_mmcm_locked"]
        or uart_physical["first_stage_exception_pin"]!="logger/rx_lock_meta_reg/D"):
        raise RuntimeError("UART first-stage exception is not scoped")
    if min(uart_physical["meta_to_sync_setup_slack_ns"],uart_physical["meta_to_sync_hold_slack_ns"],
           uart_physical["uart_output_worst_slack_ns"])<0:
        raise RuntimeError("UART physical timing failed")
    rx_diagnostics=json.loads((ROOT/"build/rx-diagnostics/verification.json").read_text())
    if not rx_diagnostics["passed"] or rx_diagnostics["counter_bits"]!=32:
        raise RuntimeError("RX diagnostics independent oracle did not pass")
    for name,digest in rx_diagnostics["rtl_sha256"].items():
        if sha(run/"rtl"/name)!=digest:
            raise RuntimeError("RX diagnostic test differs from frozen RTL: "+name)
    if sha(ROOT/"src/main/scala/iperf/RxDiagnosticSnapshot.scala")!=rx_diagnostics["input_sha256"]["RxDiagnosticSnapshot.scala"]:
        raise RuntimeError("RX diagnostic Scala differs from tested version")
    if sha(ROOT/"sim/rx_diagnostics_tb.sv")!=rx_diagnostics["input_sha256"]["rx_diagnostics_tb.sv"]:
        raise RuntimeError("RX diagnostic oracle source changed")
    if sha(ROOT/"build/rx-diagnostics/test.log")!=rx_diagnostics["test_log_sha256"]:
        raise RuntimeError("RX diagnostic test log changed")
    rx_physical=json.loads((run/"audit_rx_diagnostics_verification.json").read_text())
    if (not rx_physical["passed"] or rx_physical["held_bits"]!=448
        or rx_physical["captured_bits"]!=448 or rx_physical["mapped_bits"]!=448
        or rx_physical["timed_payload_bits"]!=448 or rx_physical["toggle_count"]!=2
        or rx_physical["reviewed_reset_fanout_count"]!=2):
        raise RuntimeError("Diagnostic mailbox physical coverage is incomplete")
    if rx_physical["payload_requirement_ns"]!=8.0 or min(
        rx_physical["payload_worst_slack_ns"],rx_physical["toggle_first_worst_slack_ns"],
        rx_physical["toggle_setup_worst_slack_ns"],rx_physical["toggle_hold_worst_slack_ns"])<0:
        raise RuntimeError("Diagnostic mailbox timing failed")
    bridge=json.loads((ROOT/"build/rx-bridge/verification.json").read_text())
    if not bridge["passed"] or bridge["frames"]!=1024 or bridge["bytes"]!=1550336:
        raise RuntimeError("Continuous MAC/async-bridge test did not pass")
    for name,digest in bridge["rtl_sha256"].items():
        if sha(run/"rtl"/name)!=digest:raise RuntimeError("Bridge test differs from frozen RTL: "+name)
    if sha(ROOT/"sim/rx_bridge_tb.sv")!=bridge["testbench_sha256"] or sha(ROOT/"build/rx-bridge/test.log")!=bridge["test_log_sha256"]:
        raise RuntimeError("Bridge oracle evidence changed")
    uart=json.loads((ROOT/"build/uart/verification.json").read_text())
    if not uart["passed"] or uart["nominal_baud"]!=460800 or uart["baud_div"]!=271:
        raise RuntimeError("Missing 460800 UART validation")
    if sha(run/"iperf_uart_log.sv")!=uart["input_sha256"]["iperf_uart_log.sv"]:
        raise RuntimeError("UART test differs from frozen RTL")
    if sha(ROOT/"sim/uart_log_tb.sv")!=uart["input_sha256"]["uart_log_tb.sv"]:
        raise RuntimeError("UART oracle differs from tested version")
    if sha(ROOT/"build/uart/test.log")!=uart["test_log_sha256"]:
        raise RuntimeError("UART log changed")
    shutil.copy2(ab_path,run/"rx-window-ab-verification.json")
    shutil.copy2(ROOT/"build/rx-bridge/verification.json",run/"rx-bridge-verification.json")
    shutil.copy2(ROOT/"build/rx-diagnostics/verification.json",run/"rx-diagnostics-verification.json")
    shutil.copy2(ROOT/"build/uart/verification.json",run/"uart-verification.json")
    shutil.copy2(ROOT/"build/functional-verification.json",run/"functional-verification.json")
    shutil.copy2(window_path,run/"tx-window-verification.json")
    shutil.copy2(model_board_path,run/"gmii-board-equivalence.json")
    shutil.copy2(receive_window_path,run/"receive-window-verification.json")
    shutil.copy2(ROOT/"build/board-reset/verification.json",run/"board-reset-verification.json")
    data={"sealed_utc":datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "run_directory":str(run),"setup_slack_ns":nums[0],"hold_slack_ns":nums[4],"pulse_width_slack_ns":nums[8],
        "setup_failing_endpoints":int(nums[2]),"hold_failing_endpoints":int(nums[6]),"pulse_width_failing_endpoints":int(nums[10]),
        "bus_skew_met":True,"cdc_reviewed_reset_critical_count":4,
        "cdc_reviewed_diagnostic_reset_fanout_count":2,
        "routing_errors":0,"drc_errors":0,"drc_critical_warnings":0,
        "tx_boundary_seed_verification_sha256":sha(seed_path),
        "tx_boundary_seed_input_sha256":sha(run/"tx_boundary_seed.tcl"),
        "tx_boundary_seed_layout_sha256":sha(run/"gold-native-layout.tsv"),
        "tx_boundary_reference_sha256":sha(run/"tx-boundary-reference.json"),
        "paired_lut_boundary_verified":True,
        "paired_lut_preplacement_verification_sha256":sha(run/"boundary-packing-verification.json") if paired_child else None,
        "final_boundary_verification_sha256":sha(run/"final-boundary-verification.json"),
        "final_boundary_properties_sha256":sha(run/"final-boundary-properties.tsv"),
        "tx_boundary_seed_applied_before_placement":True,
        "input_manifest_sha256":sha(run/"provenance.json"),
        "output_sha256":{p.name:sha(p) for p in sorted(run.glob("*.rpt"))+[bit,run/"routed.dcp",run/"audit_uart_restored.xdc",run/"audit_rx_phase.txt"]},
        "functional_verification_sha256":sha(run/"functional-verification.json"),
        "board_reset_verification_sha256":sha(run/"board-reset-verification.json"),
         "uart_verification_sha256":sha(run/"uart-verification.json"),
        "rx_diagnostics_verification_sha256":sha(run/"rx-diagnostics-verification.json"),
        "rx_bridge_verification_sha256":sha(run/"rx-bridge-verification.json"),
        "rx_diagnostics_physical_verification_sha256":sha(run/"audit_rx_diagnostics_verification.json"),
        "rx_diagnostics_audit_script_sha256":sha(run/"audit_rx_diagnostics.tcl"),
        "uart_physical_verification_sha256":sha(run/"audit_uart_verification.json"),
        "uart_audit_script_sha256":sha(run/"audit_uart.tcl"),
        "tcp_tx_banks":64,"retained_payload_bytes_at_mss1460":93440,
        "receive_buffer_bytes":262144,"congestion_cap_bytes":131072,"build_tag":"TX64-RX256K-N57B",
        "rx_window_capacity_ab_verification_sha256":sha(run/"rx-window-ab-verification.json"),
        "receive_window_verification_sha256":sha(run/"receive-window-verification.json"),
        "tx_window_verification_sha256":sha(run/"tx-window-verification.json"),
        "model_board_fir_comparison_sha256":sha(run/"gmii-board-equivalence.json"),
        "board_ip":"192.168.137.10","gateway":"192.168.137.1","uart_baud":460800,
        "implementation_retry":manifest.get("implementation_retry"),
        "native_tx_placement_verification_sha256":sha(placement_path) if placement_retry else None,
        "native_physical_scope_verification_sha256":sha(scope_path) if placement_retry else None,
        "reviewed_implementation_critical_warning_ids":review["implementation_critical_warning_ids"] if placement_retry else [],
        "clock_guidance_review_sha256":sha(review_path) if placement_retry else None,
        "rx_capture_phase_degrees":phase,"resumed_routing":reuse,
        "rx_phase_eco_sha256":sha(run/"rx_phase_eco.json") if reuse else None,
        "rx_phase_probe_sha256":sha(run/"rx_phase_probe.csv") if (run/"rx_phase_probe.csv").exists() else None,
        "board_programmed":False,"board_iperf_measured":False}
    output=json.dumps(data,indent=2)+"\n"
    (run/"physical-verification.json").write_text(output)
    (ROOT/"build/physical-verification.json").write_text(output)
    print(f"PASS physical: WNS={nums[0]}ns WHS={nums[4]}ns WPWS={nums[8]}ns; board throughput unmeasured")
if __name__=="__main__":main()
