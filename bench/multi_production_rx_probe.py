#!/usr/bin/env python3
"""Run the existing Ethernet RX deadline/byte oracle with full production RAM."""
from datetime import datetime, timezone
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    verification = ROOT / "build/verified.json"
    record = json.loads(verification.read_text())
    if "ethernet_multi_rx" not in record["tests_run"]:
        raise RuntimeError("Verify the current Ethernet multi RX target first")
    def check_sources():
        for name, sha in record["source_sha256"].items():
            if digest(ROOT / name) != sha:
                raise RuntimeError("Unverified source changed: " + name)
    check_sources()
    work = ROOT / "build" / ("multi-production-rx-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ"))
    work.mkdir()
    shutil.copytree(ROOT / "src", work / "src")
    for name in ["build.mill", ".mill-version", ".mill-jvm-opts"]:
        shutil.copy2(ROOT / name, work / name)
    shutil.copy2(verification, work / "source-verification.json")
    harness = work / "src/main/scala/netstack/MultiTcpEndpointHarness.scala"
    old = "new MultiEthernetTcpUdpEndpoint(4, 128, 4, 128, 128, 4, 10, 24, 4, true, udpPorts)"
    new = "new MultiEthernetTcpUdpEndpoint(4, 128, 8, 128, 4096, 16, 10, 24, 16, true, udpPorts)"
    text = harness.read_text()
    if text.count(old) != 1:
        raise RuntimeError("Review changed harness constructor before adapting")
    text = text.replace(old, new)
    # Widen test-only diagnostic ports to represent all production credits.
    for old_width, new_width in [
        ("new MultiTcpApplicationPort(4, 4)", "new MultiTcpApplicationPort(8, 16)"),
        ("val freeBanks = Output(UInt(3.W))", "val freeBanks = Output(UInt(4.W))"),
        ("val connectionOutstanding = Output(UInt(3.W))", "val connectionOutstanding = Output(UInt(5.W))"),
        ("val outstanding = Output(UInt(5.W))", "val outstanding = Output(UInt(7.W))"),
    ]:
        if text.count(old_width) != 1:
            raise RuntimeError("Review changed harness diagnostics")
        text = text.replace(old_width, new_width)
    harness.write_text(text)
    emitter = work / "src/main/scala/netstack/EmitMultiProductionRx.scala"
    emitter.write_text("""package netstack
import circt.stage.ChiselStage
import java.nio.file.{Files, Paths}
object EmitMultiProductionRx extends App {
  Files.writeString(Paths.get(args(0)), ChiselStage.emitCHIRRTL(new MultiEthernetTcpHarness))
}
""")
    cpp = (ROOT / "sim/ethernet_multi_rx.cpp").read_text()
    old_fill = "p.receive(0, p.pattern(1024)); p.receive(0, p.pattern(1024));"
    if cpp.count(old_fill) != 1:
        raise RuntimeError("Review changed zero-window oracle before adapting")
    cpp = cpp.replace(old_fill, "for(unsigned i=0;i<64;++i) p.receive(0, p.pattern(1024));")
    cpp = cpp.replace("== 2048", "== 65535").replace("2 KiB", "64 KiB")
    (work / "rx-probe.cpp").write_text(cpp)
    tx = (ROOT / "sim/ethernet_multi.cpp").read_text()
    if tx.count("Prefill=16") != 1 or tx.count("get_io$$freeBanks()==4") != 1:
        raise RuntimeError("Review changed TX capacity oracle")
    tx = tx.replace("Prefill=16", "Prefill=32").replace("get_io$$freeBanks()==4", "get_io$$freeBanks()==8")
    tx = tx.replace("Four banks per context", "Eight banks per context")
    (work / "tx-probe.cpp").write_text(tx)
    generated = work / "generated"
    generated.mkdir()
    env = dict(os.environ)
    source = Path(env.get("GSIM_SOURCE", "/home/openion/.cache/valence-net/gsim-src-93b8cd23"))
    revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if revision != record["gsim_revision"]:
        raise RuntimeError("GSIM revision differs from verified tool")
    fir = generated / "MultiEthernetTcpHarness.fir"
    commands = [
        (["mill", "-i", "netstack.runMain", "netstack.EmitMultiProductionRx", str(fir)], work),
        ([str(source / "build/gsim/gsim"), "--threads=1", "--dir=" + str(generated), str(fir)], ROOT),
        ([env.get("GSIM_CXX", "clang++-19"), "-std=c++20", "-O1", "-g",
          "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
          "-I" + str(generated), "-c", str(generated / "MultiEthernetTcpHarness0.cpp"),
          "-o", str(work / "model.o")], ROOT),
    ]
    for kind in ["rx", "tx"]:
        commands.append(([env.get("GSIM_CXX", "clang++-19"), "-std=c++20", "-O1", "-g",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-I" + str(generated), "-I" + str(ROOT / "sim"), str(work / "model.o"),
            str(work / (kind + "-probe.cpp")), "-ldl", "-o", str(work / (kind + "-probe"))], ROOT))
    print(work, flush=True)
    with (work / "build.log").open("w") as log:
        for command, cwd in commands:
            subprocess.run(command, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    results = {}
    for kind in ["rx", "tx"]:
        with (work / (kind + "-result.log")).open("w") as log:
            result = subprocess.run([str(work / (kind + "-probe"))], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
        results[kind] = result.returncode
    check_sources()
    files = ["source-verification.json", "rx-probe.cpp", "tx-probe.cpp",
             str(harness.relative_to(work)), str(emitter.relative_to(work)),
             str(fir.relative_to(work)), "rx-result.log", "tx-result.log"]
    provenance = {
        "scope": "Four contexts, production 8 TX banks, 64 KiB RX/context, ARP16, completion16; "
                 "test-only tick/TIME-WAIT remain short; no board signoff",
        "source_verification_sha256": digest(verification),
        "generator_sha256": digest(Path(__file__)),
        "sha256": {name: digest(work / name) for name in files},
        "commands": [command for command, _ in commands],
        "exit_codes": results,
    }
    (work / "probe-provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    for kind in ["rx", "tx"]:
        print((work / (kind + "-result.log")).read_text(), end="")
    if any(results.values()):
        raise RuntimeError("Production service check failed: " + str(results))

if __name__ == "__main__":
    main()
