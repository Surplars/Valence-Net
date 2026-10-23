#!/usr/bin/env python3
"""Freeze exported RTL and its source provenance before a Windows Vivado run."""
from pathlib import Path
from datetime import datetime, timezone
import argparse
import hashlib
import json
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
TARGETS = {
    "TcpDataTransmit": "tcp-data-rtl",
    "TcpConnectionTransmit": "tcp-connection-tx-rtl",
    "TcpIpv4Endpoint": "tcp-endpoint-rtl",
    "EthernetTcpUdpEndpoint": "ethernet-tcp-udp-rtl",
    "MultiTcpIpv4Endpoint": "multi-tcp-endpoint-rtl",
    "MultiEthernetTcpUdpEndpoint": "multi-ethernet-tcp-udp-rtl",
    "TcpRetransmissionTimer": "tcp-timer-rtl",
    "TcpManagedConnection": "tcp-managed-rtl",
    "TcpReceive": "tcp-receive-rtl",
    "EthernetUdpEndpoint": "ethernet-rtl",
}
parser = argparse.ArgumentParser()
parser.add_argument("targets", nargs="*", help="optional subset of OOC module names")
parser.add_argument("--reemit-check", action="store_true", help="re-emit RTL and byte-compare selected exports before freezing")
args = parser.parse_args()
selected = args.targets or list(TARGETS)
if any(name not in TARGETS for name in selected) or len(set(selected)) != len(selected):
    parser.error("choose unique target names from: " + ", ".join(TARGETS))
TARGETS = {name: TARGETS[name] for name in selected}
manifest = json.loads((ROOT / "build/verified.json").read_text())
for name, expected in manifest["source_sha256"].items():
    if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != expected:
        raise RuntimeError("Unverified source changed: " + name)
def current_sources():
    sources = sorted((ROOT / "src").rglob("*.scala")) + sorted((ROOT / "sim").glob("*.*")) + [ROOT / "build.mill"]
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
if current_sources() != manifest["source_sha256"]:
    raise RuntimeError("Source set changed since functional verification")
reemission = None
export_root = ROOT / "build"
if args.reemit_check:
    check = ROOT / "build/reemit" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    check.mkdir(parents=True, exist_ok=False)
    with (check / "emit.log").open("w") as log:
        result = subprocess.run(["mill", "-i", "netstack.runMain", "netstack.Emit", str(check)],
                                cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError("Fresh RTL emission failed; inspect " + str(check / "emit.log"))
    if current_sources() != manifest["source_sha256"]:
        raise RuntimeError("Sources changed during RTL re-emission")
    compared = {}
    stale_exports = {}
    for folder in TARGETS.values():
        previous = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in (ROOT / "build" / folder).iterdir() if p.suffix in {".sv", ".v"}}
        fresh = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in (check / folder).iterdir() if p.suffix in {".sv", ".v"}}
        if not fresh or any(previous.get(name) != value for name, value in fresh.items()):
            raise RuntimeError("Export differs from fresh Chisel emission: " + folder)
        compared[folder] = fresh
        stale_exports[folder] = sorted(previous.keys() - fresh.keys())
    # A previous emission can leave no-longer-instantiated module files.
    # Freeze the fresh complete module set, never stale cache leftovers.
    export_root = check
    reemission = {"matched": True, "log": str(check / "emit.log"), "exports": compared,
                  "stale_exports_ignored": stale_exports}
run = ROOT / "build/vivado" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
run.mkdir(parents=True, exist_ok=False)
(run / "targets.txt").write_text("\n".join(TARGETS) + "\n")
rtl_hashes = {}
for top, folder in TARGETS.items():
    destination = run / "input" / top
    destination.mkdir(parents=True)
    for source in sorted((export_root / folder).iterdir()):
        if source.suffix not in {".sv", ".v"}:
            continue
        shutil.copy2(source, destination / source.name)
        rtl_hashes[str((destination / source.name).relative_to(run))] = hashlib.sha256(source.read_bytes()).hexdigest()
    if not (destination / (top + ".sv")).is_file():
        raise RuntimeError("Missing exported default-geometry top: " + top)
shutil.copy2(ROOT / "fpga/ooc.tcl", run / "ooc.tcl")
(run / "provenance.json").write_text(json.dumps({
    "source_verification": manifest,
    "fresh_emission_check": reemission,
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
