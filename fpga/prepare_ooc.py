#!/usr/bin/env python3
"""Freeze exported RTL and its source provenance before a Windows Vivado run."""
from pathlib import Path
from datetime import datetime, timezone
import argparse
import hashlib
import json
import shutil

ROOT = Path(__file__).resolve().parents[1]
TARGETS = {
    "TcpDataTransmit": "tcp-data-rtl",
    "TcpManagedConnection": "tcp-managed-rtl",
    "TcpReceive": "tcp-receive-rtl",
    "EthernetUdpEndpoint": "ethernet-rtl",
}
parser = argparse.ArgumentParser()
parser.add_argument("targets", nargs="*", help="optional subset of OOC module names")
args = parser.parse_args()
selected = args.targets or list(TARGETS)
if any(name not in TARGETS for name in selected) or len(set(selected)) != len(selected):
    parser.error("choose unique target names from: " + ", ".join(TARGETS))
TARGETS = {name: TARGETS[name] for name in selected}
manifest = json.loads((ROOT / "build/verified.json").read_text())
for name, expected in manifest["source_sha256"].items():
    if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != expected:
        raise RuntimeError("Unverified source changed: " + name)
run = ROOT / "build/vivado" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
run.mkdir(parents=True, exist_ok=False)
(run / "targets.txt").write_text("\n".join(TARGETS) + "\n")
rtl_hashes = {}
for top, folder in TARGETS.items():
    destination = run / "input" / top
    destination.mkdir(parents=True)
    for source in sorted((ROOT / "build" / folder).iterdir()):
        if source.suffix not in {".sv", ".v"}:
            continue
        shutil.copy2(source, destination / source.name)
        rtl_hashes[str((destination / source.name).relative_to(run))] = hashlib.sha256(source.read_bytes()).hexdigest()
    if not (destination / (top + ".sv")).is_file():
        raise RuntimeError("Missing exported default-geometry top: " + top)
shutil.copy2(ROOT / "fpga/ooc.tcl", run / "ooc.tcl")
(run / "provenance.json").write_text(json.dumps({
    "source_verification": manifest,
    "rtl_sha256": rtl_hashes,
    "script_sha256": hashlib.sha256((run / "ooc.tcl").read_bytes()).hexdigest(),
    "targets": TARGETS,
    "part": "xczu15eg-ffvb1156-2-i",
    "period_ns": 6.4,
    "input_max_ns": 1.0,
    "input_min_ns": 0.0,
    "output_max_ns": 1.0,
    "output_min_ns": 0.0,
    "setup_uncertainty_ns": 0.1,
    "hold_uncertainty_ns": 0.05,
    "scope": "out-of-context default-geometry modules; not a routed SoC",
}, indent=2) + "\n")
print(run)
print("//wsl.localhost/Ubuntu-24.04" + str(run))
