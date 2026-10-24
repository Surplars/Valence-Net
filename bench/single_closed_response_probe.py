#!/usr/bin/env python3
"""Check single-session stateless TCP replies with a fresh production RX model."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def adapt_peer(text):
    """Keep canonical peer behavior, adding stateless classification and checks."""
    edits = [
        (" Harness d;std::mt19937_64 rng{0x10aabbcc};",
         " Harness d;std::mt19937_64 rng{0x10aabbcc};\n"
         " std::vector<std::vector<uint8_t>> exceptions;unsigned tcpRoutes=0;"),
        (" using Beat=std::tuple<uint64_t,uint64_t,unsigned,bool,bool,Meta>;",
         " using Beat=std::tuple<uint64_t,uint64_t,unsigned,bool,bool,bool,Meta>;"),
        ("bool(d.get_io$$outLast()),bool(d.get_io$$outError()),meta()};",
         "bool(d.get_io$$outLast()),bool(d.get_io$$outError()),bool(d.get_io$$outTcp()),meta()};"),
        ("     if(ip[9]==6){", """     if(ip[9]==6&&!d.get_io$$outTcp()){
      check(ip.size()==40 && ip[0]==0x45 && u16(ip,2)==40 && sum({ip.begin(),ip.begin()+20})==0,"exception IPv4 envelope/checksum");
      check(u32(ip,12)==0x0a000001 && u32(ip,16)==0x0a000002,"exception address reversal");
      std::vector<uint8_t> pseudo(ip.begin()+12,ip.begin()+20);pseudo.push_back(0);pseudo.push_back(6);pseudo.push_back(0);pseudo.push_back(20);
      pseudo.insert(pseudo.end(),ip.begin()+20,ip.end());check(sum(pseudo)==0,"exception TCP checksum");
      check(ip[32]==0x50 && (ip[33]==4||ip[33]==20) && u16(ip,34)==0 && u16(ip,38)==0,"exception flags/window/urgent");
      exceptions.push_back(ip);
     }else if(ip[9]==6){"""),
        ("  if(d.get_io$$routeValid()&&d.get_io$$routeFailed()){",
         "  if(d.get_io$$routeValid())++tcpRoutes;\n"
         "  if(d.get_io$$routeValid()&&d.get_io$$routeFailed()){"),
    ]
    for old, new in edits:
        if text.count(old) != 1:
            raise RuntimeError("Canonical Ethernet peer changed; review adaptation: " + old)
        text = text.replace(old, new)
    return text


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("production_probe", type=Path,
                        help="current production_rx_probe directory with passing reassembly probe")
    parent = parser.parse_args().production_probe.resolve()
    verified_path = ROOT / "build/verified.json"
    verified = json.loads(verified_path.read_text())
    expected = verified["source_sha256"]

    def verify_sources():
        for name, sha in expected.items():
            if digest(ROOT / name) != sha:
                raise RuntimeError("Authoritative source changed: " + name)

    verify_sources()
    provenance = json.loads((parent / "probe-provenance.json").read_text())
    reassembly = json.loads((parent / "reassembly-provenance.json").read_text())
    if provenance["exit_code"] != 0 or reassembly["exit_code"] != 0:
        raise RuntimeError("Parent capacity checks did not pass")
    parent_source = json.loads((parent / "source-verification.json").read_text())
    if parent_source["source_sha256"] != expected or reassembly["source_sha256"] != expected:
        raise RuntimeError("Parent model belongs to another source revision")
    for name, sha in provenance["sha256"].items():
        if digest(parent / name) != sha:
            raise RuntimeError("Parent probe artifact changed: " + name)
    if digest(parent / "probe-provenance.json") != reassembly["probe_provenance_sha256"]:
        raise RuntimeError("Parent model and reassembly provenance disagree")
    model = parent / "generated"
    if digest(model / "EthernetServiceHarness0.cpp") != reassembly["generated_cpp_sha256"]:
        raise RuntimeError("Parent generated model changed")
    if digest(parent / "reassembly-result.log") != reassembly["result_sha256"]:
        raise RuntimeError("Parent reassembly result changed")
    geometry = (parent / "src/main/scala/netstack/EthernetTcpHarness.scala").read_text()
    if "new EthernetTcpUdpEndpoint(128, txBanks, 128, 4096, 4, 10, 24, 4, autonomous)" not in geometry:
        raise RuntimeError("Production receive geometry changed; review the probe")
    endpoint = (ROOT / "src/main/scala/netstack/TcpIpv4Endpoint.scala").read_text()
    link = (ROOT / "src/main/scala/netstack/EthernetTcpUdpEndpoint.scala").read_text()
    if "val closedResponses: Boolean = true" not in endpoint or (
        "new TcpIpv4Endpoint(txBanks, txBeats, rxWords, packetBeats, ticksPerSecond, "
        "timeWaitTicks, completionDepth = completionDepth, autonomousCongestion = autonomousCongestion)"
    ) not in link:
        raise RuntimeError("Single endpoint closed-response default/instantiation changed")

    gsim = Path(os.environ.get("GSIM_SOURCE", "/home/openion/Valence/simulator/build/gsim-src"))
    revision = subprocess.check_output(
        ["git", "-C", str(gsim), "rev-parse", "HEAD"], text=True).strip()
    if revision != verified["gsim_revision"]:
        raise RuntimeError("GSIM revision differs from functional verification")
    inputs = [Path(__file__).resolve(), ROOT / "bench/single_closed_response.cpp",
              ROOT / "sim/ethernet_peer.h", ROOT / "sim/gsim_model_init.h"]
    input_hashes = {str(p.relative_to(ROOT)): digest(p) for p in inputs}
    model_hashes = {str(p): digest(p) for p in model.iterdir()
                    if p.suffix in [".h", ".cpp", ".fir"]}
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    work = ROOT / "build" / ("single-closed-response-probe-" + stamp)
    work.mkdir()
    shutil.copy2(ROOT / "bench/single_closed_response.cpp", work / "single_closed_response.cpp")
    (work / "ethernet_peer.h").write_text(adapt_peer((ROOT / "sim/ethernet_peer.h").read_text()))
    shutil.copy2(ROOT / "sim/gsim_model_init.h", work / "gsim_model_init.h")
    shutil.copy2(verified_path, work / "source-verification.json")
    shutil.copy2(parent / "probe-provenance.json", work / "model-provenance.json")
    compiler = os.environ.get("GSIM_CXX", "clang++-19")
    flags = ["-std=c++20", "-O1", "-g", "-fsanitize=address,undefined",
             "-fno-sanitize-recover=all"]
    commands = [
        [compiler, *flags, "-I" + str(work), "-I" + str(model), "-c",
         str(model / "EthernetServiceHarness0.cpp"), "-o", str(work / "model.o")],
        [compiler, *flags, "-I" + str(work), "-I" + str(model), str(work / "model.o"),
         str(work / "single_closed_response.cpp"), "-ldl", "-o", str(work / "probe")],
    ]
    print(work, flush=True)
    with (work / "build.log").open("w") as log:
        for command in commands:
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (work / "result.log").open("w") as log:
        result = subprocess.run([str(work / "probe")], cwd=ROOT,
                                stdout=log, stderr=subprocess.STDOUT)
    verify_sources()
    for name, sha in {**input_hashes, **model_hashes}.items():
        if digest(ROOT / name) != sha:
            raise RuntimeError("Probe input changed during execution: " + name)
    if digest(verified_path) != digest(work / "source-verification.json"):
        raise RuntimeError("Canonical verification manifest changed")
    files = ["source-verification.json", "model-provenance.json", "gsim_model_init.h",
             "ethernet_peer.h", "single_closed_response.cpp", "build.log", "result.log"]
    (work / "provenance.json").write_text(json.dumps({
        "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "scope": "Single EthernetTcpUdpEndpoint closedResponses=true; freshly emitted production "
                 "RX snapshot with 64KiB ring; other reduced harness parameters unchanged",
        "source_sha256": expected, "inputs_sha256": input_hashes,
        "gsim_revision": revision, "gsim_binary_sha256": digest(gsim / "build/gsim/gsim"),
        "compiler": subprocess.check_output([compiler, "--version"], text=True).splitlines()[0],
        "root_verified_sha256": digest(verified_path), "parent_model_directory": str(parent),
        "parent_model_provenance_sha256": digest(parent / "probe-provenance.json"),
        "parent_reassembly_provenance_sha256": digest(parent / "reassembly-provenance.json"),
        "model_sha256": model_hashes,
        "local_helper_adaptation": "Checked edits add stateless RST classification/checksums, "
            "outTcp backpressure stability and route counting; model uses gsim_model_init.h",
        "artifacts_sha256": {name: digest(work / name) for name in files},
        "commands": commands + [[str(work / "probe")]],
        "exit_code": result.returncode, "asan_ubsan": True, "authoritative_source_unchanged": True,
    }, indent=2) + "\n")
    print((work / "result.log").read_text(), end="", flush=True)
    result.check_returncode()


if __name__ == "__main__":
    main()
