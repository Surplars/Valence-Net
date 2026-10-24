#!/usr/bin/env python3
"""Freeze an immutable routed stage checkpoint from an unfinished OOC run.

The parent is never relabeled complete. The refinement Tcl independently checks
that the saved checkpoint is fully routed before doing local physical work.
"""
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

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("reference_run", type=Path)
    args = parser.parse_args()
    parent = args.reference_run.resolve()
    proof = json.loads((parent / "provenance.json").read_text())
    manifest = json.loads((ROOT / "build/verified.json").read_text())
    files = sorted((ROOT / "src").rglob("*.scala")) + sorted((ROOT / "sim").glob("*.*")) + [ROOT / "build.mill"]
    current = {str(p.relative_to(ROOT)): digest(p) for p in files}
    if current != manifest["source_sha256"] or current != proof["source_verification"]["source_sha256"]:
        raise RuntimeError("Saved checkpoint and current functional sources differ")
    if proof["part"] != "xczu15eg-ffvb1156-2-i" or proof["period_ns"] != 6.4:
        raise RuntimeError("Reference target differs")
    if list(proof["targets"]) != [TOP] or proof.get("core_module") != "MultiEthernetTcpUdpEndpoint":
        raise RuntimeError("Expected full production four-context timing fixture")
    windows = Path("/mnt/e/VM/Share/Valence-rtl") / ("hardware-netstack-ooc-" + parent.name)
    if digest(parent / "ooc.tcl") != proof["script_sha256"] or digest(windows / "ooc.tcl") != proof["script_sha256"]:
        raise RuntimeError("Reference script changed")
    for name, expected in proof["rtl_sha256"].items():
        if digest(parent / name) != expected or digest(windows / name) != expected:
            raise RuntimeError("Reference RTL changed: " + name)
    checkpoint = windows / TOP / "baseline_route.dcp"
    timing = windows / TOP / "baseline_route_timing.rpt"
    constraints = windows / TOP / "constraints.xdc"
    if not checkpoint.is_file() or checkpoint.stat().st_size < 1024 or not timing.is_file():
        raise RuntimeError("No completed saved routing stage")
    if "Design State      : Routed" not in timing.read_text():
        raise RuntimeError("Saved timing report is not a routed design")
    run = ROOT / "build/vivado" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    run.mkdir(parents=True, exist_ok=False)
    shutil.copytree(parent / "input", run / "input")
    shutil.copy2(checkpoint, run / "input/routed_reference.dcp")
    shutil.copy2(constraints, run / "input/reference_constraints.xdc")
    if digest(checkpoint) != digest(run / "input/routed_reference.dcp"):
        raise RuntimeError("Checkpoint changed while freezing")
    script = ROOT / "fpga/refine_routed.tcl"
    shutil.copy2(script, run / "ooc.tcl")
    (run / "targets.txt").write_text(TOP + "\n")
    record = dict(proof)
    record.update({
        "source_verification": manifest,
        "script_sha256": digest(run / "ooc.tcl"),
        "scope": "Independent local refinement of a saved fully routed production checkpoint; parent flow incomplete; original signoff unchanged",
        "checkpoint_sha256": {
            "input/routed_reference.dcp": digest(run / "input/routed_reference.dcp"),
            "input/reference_constraints.xdc": digest(run / "input/reference_constraints.xdc"),
        },
        "routed_reference": {
            "run": str(parent), "stage": "baseline_route",
            "checkpoint_sha256": digest(checkpoint), "saved_timing_sha256": digest(timing),
            "provenance_sha256": digest(parent / "provenance.json"),
            "parent_progress_at_freeze": (windows / "progress.txt").read_text().strip(),
            "parent_completed": False,
        },
        "refinement": {
            "preparer_sha256": digest(Path(__file__)),
            "starting_routing_checked_by_tcl": True,
            "optimization_setup_uncertainty_attempts_ns": [0.100, 0.150, 0.200],
            "signoff_setup_uncertainty_ns": 0.100, "signoff_hold_uncertainty_ns": 0.050,
        },
    })
    if current != {str(p.relative_to(ROOT)): digest(p) for p in files}:
        raise RuntimeError("Sources changed while freezing")
    (run / "provenance.json").write_text(json.dumps(record, indent=2) + "\n")
    print(run)

if __name__ == "__main__":
    main()
