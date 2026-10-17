#!/usr/bin/env python3
"""Run standalone RTL checks in WSL using the existing pinned GSIM toolchain."""
import os
import hashlib
import json
from pathlib import Path
import subprocess
import argparse

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
GSIM_SOURCE = Path(os.environ.get("GSIM_SOURCE", "/home/openion/Valence/simulator/build/gsim-src"))
REVISION = "93b8cd23edd3228807c4f2a08c19c3936a463cb2"

def source_hashes():
    sources = sorted((ROOT / "src").rglob("*.scala")) + sorted((ROOT / "sim").glob("*.*")) + [ROOT / "build.mill"]
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}

def run(*args, cwd=ROOT, log=None):
    print("+", " ".join(map(str, args)), flush=True)
    if log:
        with log.open("w") as stream:
            result = subprocess.run(list(map(str, args)), cwd=cwd, stdout=stream, stderr=subprocess.STDOUT)
        if result.returncode:
            print(log.read_text()[-12000:])
            result.check_returncode()
    else:
        subprocess.run(list(map(str, args)), cwd=cwd, check=True)

def main():
    targets = {"checksum": "ChecksumHarness", "udp": "UdpHarness", "buffer": "BufferHarness",
               "receive": "ReceiveHarness", "slice": "SliceHarness", "ip_receive": "IpReceiveHarness",
               "transmit": "TransmitHarness", "arp": "ArpHarness", "ethernet": "EthernetHarness",
               "tcp_receive": "TcpReceiveHarness", "tcp_transmit": "TcpTransmitHarness",
               "tcp_timer": "TcpTimerHarness", "tcp_store": "TcpStoreHarness", "tcp_control": "TcpControlHarness", "tcp_managed": "TcpManagedHarness", "tcp_data": "TcpDataHarness"}
    parser = argparse.ArgumentParser()
    parser.add_argument("targets", nargs="*", help="focused tests; omit for all")
    args = parser.parse_args()
    selected = args.targets or list(targets)
    if any(name not in targets for name in selected):
        parser.error("unknown target; choose: " + ", ".join(targets))
    revision = subprocess.check_output(["git", "-C", str(GSIM_SOURCE), "rev-parse", "HEAD"], text=True).strip()
    if revision != REVISION:
        raise RuntimeError(f"GSIM revision mismatch: {revision}")
    BUILD.mkdir(exist_ok=True)
    (BUILD / "verified.json").unlink(missing_ok=True)
    snapshot = source_hashes()
    run("mill", "-i", "netstack.runMain", "netstack.Emit", BUILD)
    for name in selected:
        top = targets[name]
        # Only generated translation units owned by this build are replaced.
        for old in BUILD.glob(top + "[0-9]*.cpp"):
            old.unlink()
        run(GSIM_SOURCE / "build/gsim/gsim", "--threads=1", f"--dir={BUILD}", BUILD / f"{top}.fir",
            log=BUILD / f"{name}-generate.log")
        run(os.environ.get("GSIM_CXX", "clang++-19"), "-std=c++20", "-O1", "-g",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all", f"-I{BUILD}",
            *sorted(BUILD.glob(top + "[0-9]*.cpp")), ROOT / f"sim/{name}.cpp", "-ldl", "-o", BUILD / f"{name}-test")
        run(BUILD / f"{name}-test", log=BUILD / f"{name}-test.log")
        print((BUILD / f"{name}-test.log").read_text(), end="")
    if source_hashes() != snapshot:
        raise RuntimeError("Sources changed during verification; no result manifest written. Rerun the affected tests.")
    (BUILD / "verified.json").write_text(json.dumps({
        "gsim_revision": revision,
        "compiler": subprocess.check_output([os.environ.get("GSIM_CXX", "clang++-19"), "--version"], text=True).splitlines()[0],
        "source_sha256": snapshot,
        "tests_run": selected,
        "results": {name: (BUILD / f"{name}-test.log").read_text().strip() for name in selected},
        "physical_timing_verified": False,
        "complete_protocol_stack": False,
    }, indent=2) + "\n")

if __name__ == "__main__":
    main()
