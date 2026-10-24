#!/usr/bin/env python3
"""Run standalone RTL checks in WSL using the existing pinned GSIM toolchain."""
import os
import hashlib
import json
from pathlib import Path
import subprocess
import argparse
import shutil
import re

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
GSIM_SOURCE = Path(os.environ.get("GSIM_SOURCE", "/home/openion/Valence/simulator/build/gsim-src"))
REVISION = "93b8cd23edd3228807c4f2a08c19c3936a463cb2"

def source_hashes():
    sources = sorted((ROOT / "src").rglob("*.scala")) + sorted((ROOT / "sim").glob("*.*")) + [ROOT / "build.mill"]
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}

def generated_inputs(sources):
    # Follow local includes instead of hashing unrelated models' headers: a
    # different test top must not invalidate an otherwise identical object.
    pending = list(sources)
    found = set()
    while pending:
        source = pending.pop().resolve()
        if source in found:
            continue
        found.add(source)
        for name in re.findall(r'^\s*#\s*include\s*"([^"\n]+)"', source.read_text(), re.MULTILINE):
            header = source.parent / name
            if not header.is_file():
                header = BUILD / name
            if not header.is_file():
                raise RuntimeError("Missing generated model include: " + name)
            pending.append(header)
    return sorted(found)

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
               "tcp_timer": "TcpTimerHarness", "tcp_timer_pipeline": "TcpPipelinedTimerHarness", "tcp_store": "TcpStoreHarness", "tcp_control": "TcpControlHarness", "tcp_managed": "TcpManagedHarness", "tcp_data": "TcpDataHarness", "tcp_connection_tx": "TcpConnectionTxHarness", "tcp_ring": "TcpRingHarness", "tcp_endpoint": "TcpEndpointHarness", "tcp_reassembly": "TcpCongestedEndpointHarness", "ethernet_tcp": "EthernetTcpHarness", "tcp_congestion": "TcpCongestionHarness", "tcp_congested_endpoint": "TcpCongestedEndpointHarness", "ethernet_mixed": "EthernetTcpHarness", "ethernet_service": "EthernetServiceHarness", "tcp_header_snapshot": "TcpHeaderSnapshotHarness", "tcp_sack": "TcpCongestedEndpointHarness", "tcp_multi": "MultiTcpEndpointHarness", "ethernet_multi": "MultiEthernetTcpHarness", "ethernet_multi_rx": "MultiEthernetTcpHarness"}
    targets.update({"icmp_errors": "IcmpErrorsHarness", "tcp_closed": "TcpClosedHarness",
                    "tcp_exception": "MultiTcpEndpointHarness", "tcp_pmtu": "TcpPmtuHarness", "ethernet_errors": "MultiEthernetPortHarness", "network_errors": "NetworkErrorsHarness"})
    targets.update({"tcp_pmtu_pipeline": "TcpPmtuHarness"})
    targets.update({"window_scale": "TcpControlHarness", "body_window": "TcpControlHarness",
                    "tcp_generation": "TcpGenerationHarness"})
    targets.update({"tcp_control_priority": "TcpControlHarness"})
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
    cxx = os.environ.get("GSIM_CXX", "clang++-19")
    compiler = subprocess.check_output([cxx, "--version"], text=True)
    flags = ["-std=c++20", "-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
    cache = BUILD / "model-cache"
    cache.mkdir(exist_ok=True)
    models = {}
    model_records = {}
    for name in selected:
        top = targets[name]
        if top not in models:
            # Generate a fresh model. Reuse only objects whose complete model,
            # generated headers, compiler identity and flags match exactly.
            for old in BUILD.glob(top + "[0-9]*.cpp"):
                old.unlink()
            run(GSIM_SOURCE / "build/gsim/gsim", "--threads=1", f"--dir={BUILD}", BUILD / f"{top}.fir",
                log=BUILD / f"{name}-generate.log")
            sources = sorted(BUILD.glob(top + "[0-9]*.cpp"))
            model_inputs = generated_inputs(sources)
            identity = {"compiler": compiler, "compiler_path": shutil.which(cxx), "flags": flags,
                        "inputs": {str(p.name): hashlib.sha256(p.read_bytes()).hexdigest() for p in model_inputs}}
            fingerprint = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
            objects = []
            for index, source in enumerate(sources):
                obj = cache / f"{fingerprint}-{index}.o"
                seal = obj.with_suffix(".sha256")
                if not (obj.exists() and seal.exists() and
                        hashlib.sha256(obj.read_bytes()).hexdigest() == seal.read_text().strip()):
                    temporary = obj.with_suffix(".tmp.o")
                    run(cxx, *flags, f"-I{BUILD}", "-c", source, "-o", temporary)
                    temporary.replace(obj)
                    seal.write_text(hashlib.sha256(obj.read_bytes()).hexdigest() + "\n")
                else:
                    print("+ verified model object cache", top, index, flush=True)
                objects.append(obj)
            models[top] = objects
            model_records[top] = {"fingerprint": fingerprint, "identity": identity,
                                  "object_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in objects}}
        run(cxx, *flags, f"-I{BUILD}", *models[top], ROOT / f"sim/{name}.cpp", "-ldl", "-o", BUILD / f"{name}-test")
        run(BUILD / f"{name}-test", log=BUILD / f"{name}-test.log")
        print((BUILD / f"{name}-test.log").read_text(), end="")
    if source_hashes() != snapshot:
        raise RuntimeError("Sources changed during verification; no result manifest written. Rerun the affected tests.")
    (BUILD / "verified.json").write_text(json.dumps({
        "gsim_revision": revision,
        "compiler": compiler.splitlines()[0],
        "generated_models": model_records,
        "source_sha256": snapshot,
        "tests_run": selected,
        "results": {name: (BUILD / f"{name}-test.log").read_text().strip() for name in selected},
        "physical_timing_verified": False,
        "complete_protocol_stack": False,
    }, indent=2) + "\n")

if __name__ == "__main__":
    main()
