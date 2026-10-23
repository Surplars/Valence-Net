#!/usr/bin/env python3
"""Reuse a verified production-capacity RX model for full-MSS reassembly tests."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("probe", type=Path, help="completed production_rx_probe directory")
    args = parser.parse_args()
    work = args.probe.resolve()
    provenance = json.loads((work / "probe-provenance.json").read_text())
    if provenance["exit_code"] != 0:
        raise RuntimeError("RX service probe did not pass")
    for name, expected in provenance["sha256"].items():
        if digest(work / name) != expected:
            raise RuntimeError("Probe artifact changed: " + name)
    verified = json.loads((work / "source-verification.json").read_text())
    current = json.loads((ROOT / "build/verified.json").read_text())
    if verified["source_sha256"] != current["source_sha256"]:
        raise RuntimeError("Probe model belongs to another source revision")
    for name, expected in current["source_sha256"].items():
        if digest(ROOT / name) != expected:
            raise RuntimeError("Authoritative source changed: " + name)
    source = ROOT / "bench/production_reassembly.cpp"
    inputs = {str(source): digest(source), str(ROOT / "sim/ethernet_peer.h"): digest(ROOT / "sim/ethernet_peer.h")}
    model = work / "generated"
    command = [os.environ.get("GSIM_CXX", "clang++-19"), "-std=c++20", "-O1", "-g",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
        "-I" + str(model), "-I" + str(ROOT / "sim"),
        str(model / "EthernetServiceHarness0.cpp"), str(source), "-ldl",
        "-o", str(work / "reassembly-probe")]
    with (work / "reassembly-build.log").open("w") as log:
        subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (work / "reassembly-result.log").open("w") as log:
        result = subprocess.run([str(work / "reassembly-probe")], cwd=ROOT,
            stdout=log, stderr=subprocess.STDOUT)
    for name, expected in {**current["source_sha256"], **inputs}.items():
        if digest(ROOT / name) != expected:
            raise RuntimeError("Inputs changed during probe: " + name)
    (work / "reassembly-provenance.json").write_text(json.dumps({
        "scope": provenance["scope"], "source_sha256": current["source_sha256"],
        "probe_provenance_sha256": digest(work / "probe-provenance.json"),
        "script_sha256": digest(Path(__file__)), "inputs_sha256": inputs,
        "generated_cpp_sha256": digest(model / "EthernetServiceHarness0.cpp"),
        "command": command, "exit_code": result.returncode,
        "result_sha256": digest(work / "reassembly-result.log"),
    }, indent=2) + "\n")
    print((work / "reassembly-result.log").read_text(), end="")
    result.check_returncode()

if __name__ == "__main__":
    main()
