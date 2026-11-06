#!/usr/bin/env python3
"""Freeze a source/RTL snapshot into the user's dedicated Vivado directory."""
import argparse,datetime,hashlib,json,pathlib,re,shutil,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1]
DEST=pathlib.Path("/mnt/e/VM/Share/Valence-rtl")
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser()
    resume=ap.add_mutually_exclusive_group()
    resume.add_argument("--from-synth",type=pathlib.Path)
    resume.add_argument("--from-routed",type=pathlib.Path)
    args=ap.parse_args()
    for name in ["app","gmii"]:
        if not (ROOT/"build"/name/"interop.json").is_file():
            raise RuntimeError("Missing "+name+" interoperation result")
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    target=DEST/("valence-iperf-"+stamp);target.mkdir()
    subprocess.run(["mill","-i","hw.runMain","iperf.Emit","board",str(ROOT/"build/board")],cwd=ROOT,check=True)
    shutil.copytree(ROOT/"build/board/rtl",target/"rtl")
    for name in ["iperf_board.sv","iperf_uart_log.sv","board.xdc","cdc.tcl","build.tcl","audit.tcl","audit_uart.tcl","audit_rx_diagnostics.tcl","tx_boundary_seed.tcl","gold-native-layout.tsv","tx-boundary-reference.json"]:
        shutil.copy2(ROOT/"fpga"/name,target/name)
    base=ROOT/"deps/valence/fpga/zu15eg"
    for name in ["native_rgmii.sv","native_gmac_pins.xdc","cdc_constraints.tcl","native_gmac_clocks.sv","native_gmac_divided_clock.sv","native_quarter_clock_constraints.tcl"]:
        override=ROOT/"fpga"/name
        shutil.copy2(override if name=="native_gmac_clocks.sv" and override.exists() else base/name,target/name)
    def rx_phase(text):
        body=text.split("module native_gmac_rx_clock",1)[1].split("endmodule",1)[0]
        return float(re.search(r"\.CLKOUT0_PHASE\(([-0-9.]+)\)",body)[1])
    selected_phase=rx_phase((target/"native_gmac_clocks.sv").read_text())
    resumed=None
    routed_resume=None
    if args.from_routed:
        old=args.from_routed.resolve()
        if DEST.resolve() not in old.parents:raise RuntimeError("Checkpoint must be under valence-rtl")
        previous=json.loads((old/"provenance.json").read_text())
        sealed=json.loads((old/"physical-verification.json").read_text())
        for rel,digest in previous["input_sha256"].items():
            if sha(old/rel)!=digest:raise RuntimeError("Baseline frozen input changed: "+rel)
            hardware=rel.startswith("rtl/") or rel in ["iperf_board.sv","iperf_uart_log.sv","board.xdc","cdc.tcl",
                "native_rgmii.sv","native_gmac_pins.xdc","cdc_constraints.tcl","native_gmac_clocks.sv",
                "native_gmac_divided_clock.sv","native_quarter_clock_constraints.tcl"]
            if hardware and rel!="native_gmac_clocks.sv" and sha(target/rel)!=digest:
                raise RuntimeError("Routed reuse changes more than RX phase: "+rel)
        before_text=(old/"native_gmac_clocks.sv").read_text()
        after_text=(target/"native_gmac_clocks.sv").read_text()
        before_phase=rx_phase(before_text)
        # Ignore comments, require the sole Verilog token change to be RX phase.
        strip=lambda text: re.sub(r"\s+", " ", re.sub(r"//[^\n]*", "",text)).strip()
        expected=re.sub(r"(module native_gmac_rx_clock[\s\S]*?\.CLKOUT0_PHASE\()[-0-9.]+(\))",
            lambda m:m[1]+str(selected_phase)+m[2],strip(before_text),count=1)
        if expected!=strip(after_text):raise RuntimeError("Clock override changes more than RX phase")
        checkpoint=old/"routed.dcp"
        if sha(checkpoint)!=sealed["output_sha256"]["routed.dcp"]:raise RuntimeError("Baseline DCP hash changed")
        shutil.copy2(checkpoint,target/"imported_routed.dcp")
        script=f"""# Sole physical ECO: move RX sampling clock; preserve placement/routing.
set before_phase [get_property CLKOUT0_PHASE [get_cells rx_clocks/mmcm]]
if {{abs($before_phase - {before_phase})>0.0001}} {{error "Unexpected checkpoint RX phase"}}
set cells_before [llength [get_cells -hier]]
set nets_before [llength [get_nets -hier]]
set_property CLKOUT0_PHASE {selected_phase} [get_cells rx_clocks/mmcm]
set cells_after [llength [get_cells -hier]]
set nets_after [llength [get_nets -hier]]
if {{$cells_before!=$cells_after || $nets_before!=$nets_after}} {{error "Phase ECO changed design topology"}}
set f [open rx_phase_eco.json w]
puts $f [format {{{{"phase_before":%.6f,"phase_after":%.6f,"cells_before":%d,"cells_after":%d,"nets_before":%d,"nets_after":%d}}}} $before_phase {selected_phase} $cells_before $cells_after $nets_before $nets_after]
close $f
"""
        (target/"route_reuse.tcl").write_text(script)
        routed_resume={"directory":str(old),"checkpoint_sha256":sha(checkpoint),
            "baseline_physical_verification_sha256":sha(old/"physical-verification.json"),
            "rx_phase_before_deg":before_phase,"rx_phase_after_deg":selected_phase,
            "scope":"Only MMCM RX CLKOUT0_PHASE; all hardware sources otherwise identical"}
    if args.from_synth:
        old=args.from_synth.resolve()
        if DEST.resolve() not in old.parents:raise RuntimeError("Checkpoint must be under valence-rtl")
        previous=json.loads((old/"provenance.json").read_text())
        for rel,digest in previous["input_sha256"].items():
            if rel.startswith("rtl/") or rel in ["iperf_board.sv","iperf_uart_log.sv","board.xdc","native_rgmii.sv","native_gmac_pins.xdc","native_gmac_clocks.sv","native_gmac_divided_clock.sv"]:
                if sha(target/rel)!=digest:raise RuntimeError("Synthesis input changed: "+rel)
        checkpoint=old/"synth_unconstrained.dcp"
        shutil.copy2(checkpoint,target/"imported_synth.dcp")
        resumed={"directory":str(old),"checkpoint_sha256":sha(checkpoint)}
    sources=sorted((ROOT/"src").rglob("*.scala"))+sorted((ROOT/"fpga").glob("*"))+[ROOT/"build.mill"]
    stack_source=(ROOT/"src/main/scala/iperf/IperfStack.scala").read_text()
    app_source=(ROOT/"src/main/scala/iperf/Iperf3Application.scala").read_text()
    tx_banks=int(re.search(r"class IperfStack\(val txBanks: Int = (\d+)",stack_source)[1])
    rx_words=int(re.search(r"rxWords = (\d+)",stack_source)[1])
    congestion_cap=int(re.search(r"p\.congestionWindow := (\d+)\.U",app_source)[1])
    local_scale=int(re.search(r"p\.open\.bits\.localScale := (\d+)\.U",app_source)[1])
    metadata={
        "transport":{"tx_banks":tx_banks,"rx_words":rx_words,"receive_bytes":rx_words*16,
            "congestion_cap_bytes":congestion_cap,"local_window_scale":local_scale,
            "build_tag":"TX64-RX256K-N57M"},
        "project":str(ROOT),"created_utc":stamp,"resumed_synthesis":resumed,
        "resumed_routing":routed_resume,"rx_capture_phase_degrees":selected_phase,
        "dependencies":{n:subprocess.check_output(["git","-C",str(ROOT/"deps"/n),"rev-parse","HEAD"],text=True).strip()
            for n in ["valence","valence-net"]},
        "source_sha256":{str(p.relative_to(ROOT)):sha(p) for p in sources if p.is_file()},
        "input_sha256":{str(p.relative_to(target)):sha(p) for p in target.rglob("*") if p.is_file()},
        "tests":{n:json.loads((ROOT/"build"/n/"interop.json").read_text()) for n in ["app","gmii"]},
        "physical_timing_verified":False,"board_iperf_measured":False
    }
    (target/"provenance.json").write_text(json.dumps(metadata,indent=2)+"\n")
    (ROOT/"build/latest-vivado.txt").write_text(str(target)+"\n")
    print(target)
if __name__=="__main__":main()
