#!/usr/bin/env python3
"""Freeze a source-matched routed checkpoint for local physical refinement."""
from pathlib import Path
from datetime import datetime, timezone
import argparse
import hashlib
import json
import shutil

ROOT = Path(__file__).resolve().parents[1]
TOP = "ValenceNetTimingTop"

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def source_hashes():
    files = sorted((ROOT / "src").rglob("*.scala")) + sorted((ROOT / "sim").glob("*.*")) + [ROOT / "build.mill"]
    return {str(p.relative_to(ROOT)): digest(p) for p in files}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("reference_run", type=Path)
    args = parser.parse_args()
    parent = args.reference_run.resolve()
    proof = json.loads((parent / "provenance.json").read_text())
    verified = json.loads((ROOT / "build/verified.json").read_text())
    expected = verified["source_sha256"]
    if source_hashes() != expected or proof["source_verification"]["source_sha256"] != expected:
        raise RuntimeError("Reference checkpoint and current functional source differ")
    if proof["part"] != "xczu15eg-ffvb1156-2-i" or proof["period_ns"] != 6.4:
        raise RuntimeError("Reference timing target differs")
    if list(proof["targets"]) != [TOP] or proof.get("core_module") != "MultiEthernetTcpUdpEndpoint":
        raise RuntimeError("Expected full production multi-connection fixture")
    if (parent / "progress.txt").read_text().strip() != "completed":
        raise RuntimeError("Reference run is incomplete")
    if digest(parent / "ooc.tcl") != proof["script_sha256"]:
        raise RuntimeError("Reference script changed")
    for name, expected_digest in {**proof["rtl_sha256"], **proof.get("checkpoint_sha256", {})}.items():
        if digest(parent / name) != expected_digest:
            raise RuntimeError("Reference RTL changed: " + name)
    summary = json.loads((parent / "summary.json").read_text())
    module = summary["modules"][TOP]
    if not (module["routing_clean"] and module["timing_coverage_clean"] and
            module["blackboxes"] == 0 and module["drc_errors"] == 0 and
            module["methodology_errors"] == 0):
        raise RuntimeError("Reference must have complete routing and clean coverage/DRC")
    if summary.get("tool_log_errors") or summary.get("native_console_errors"):
        raise RuntimeError("Reference tool errors require resolution")
    checkpoint = parent / TOP / "post_route.dcp"
    run = ROOT / "build/vivado" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    run.mkdir(parents=True, exist_ok=False)
    shutil.copytree(parent / "input", run / "input")
    destination = run / "input/routed_reference.dcp"
    shutil.copy2(checkpoint, destination)
    constraints = run / "input/reference_constraints.xdc"
    shutil.copy2(parent / TOP / "constraints.xdc", constraints)
    script = ROOT / "fpga/refine_routed.tcl"
    shutil.copy2(script, run / "ooc.tcl")
    (run / "targets.txt").write_text(TOP + "\n")
    record = dict(proof)
    record.update({
        "source_verification": verified,
        "script_sha256": digest(run / "ooc.tcl"),
        "scope": "Source-matched fully routed production core; local physical refinement, "
                 "required 6.4ns / 0.100ns setup / 0.050ns hold signoff unchanged",
        "checkpoint_sha256": {
            "input/routed_reference.dcp": digest(destination),
            "input/reference_constraints.xdc": digest(constraints),
        },
        "routed_reference": {
            "run": str(parent),
            "checkpoint_sha256": digest(checkpoint),
            "provenance_sha256": digest(parent / "provenance.json"),
            "summary_sha256": digest(parent / "summary.json"),
        },
        "refinement": {
            "preparer_sha256": digest(Path(__file__)),
            "optimization_setup_uncertainty_attempts_ns": [0.100, 0.150, 0.200],
            "signoff_setup_uncertainty_ns": 0.100,
            "signoff_hold_uncertainty_ns": 0.050,
            "skip_global_reroute_only_when_all_nets_fully_routed": True,
        },
    })
    if source_hashes() != verified["source_sha256"]:
        raise RuntimeError("Sources changed while freezing refinement")
    (run / "provenance.json").write_text(json.dumps(record, indent=2) + "\n")
    print(run)
    print("//wsl.localhost/Ubuntu-24.04" + str(run))

if __name__ == "__main__":
    main()
