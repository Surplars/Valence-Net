#!/usr/bin/env python3
"""Measure full-endpoint TCP RX using production RX capacity in an isolated copy."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    verified_path = ROOT / "build/verified.json"
    verified = json.loads(verified_path.read_text())
    for name, expected in verified["source_sha256"].items():
        if digest(ROOT / name) != expected:
            raise RuntimeError("Run sim/run.py for current sources before this probe: " + name)
    if "ethernet_service" not in verified["tests_run"]:
        raise RuntimeError("The source verification must include ethernet_service")
    gsim_source = Path(os.environ.get("GSIM_SOURCE",
        "/home/openion/Valence/simulator/build/gsim-src"))
    revision = subprocess.check_output(
        ["git", "-C", str(gsim_source), "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    if revision != verified["gsim_revision"]:
        raise RuntimeError("GSIM revision differs from functional verification")
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    work = ROOT / "build" / ("production-rx-probe-" + stamp)
    work.mkdir()
    shutil.copytree(ROOT / "src", work / "src")
    for name in ["build.mill", ".mill-version", ".mill-jvm-opts"]:
        shutil.copyfile(ROOT / name, work / name)
    shutil.copyfile(verified_path, work / "source-verification.json")
    harness = work / "src/main/scala/netstack/EthernetTcpHarness.scala"
    old = "new EthernetTcpUdpEndpoint(128, txBanks, 128, 64, 4, 10, 24, 4, autonomous)"
    new = "new EthernetTcpUdpEndpoint(128, txBanks, 128, 4096, 4, 10, 24, 4, autonomous)"
    text = harness.read_text()
    if text.count(old) != 1:
        raise RuntimeError("Harness geometry changed; review the probe adaptation")
    harness.write_text(text.replace(old, new))
    emitter = work / "src/main/scala/netstack/EmitProductionRxProbe.scala"
    emitter.write_text("""package netstack
import circt.stage.ChiselStage
import java.nio.file.{Files, Paths}
object EmitProductionRxProbe extends App {
  val out = Paths.get(args(0)); Files.createDirectories(out)
  Files.writeString(out.resolve("EthernetServiceHarness.fir"),
    ChiselStage.emitCHIRRTL(new EthernetServiceHarness))
}
""")
    # Reuse the independent Ethernet peer and TX/UDP checks. Replace only the
    # original one-byte RX measurement, sampling each packet's final byte.
    cpp = (ROOT / "sim/ethernet_service.cpp").read_text()
    start = cpp.index(" {\n  Test t;t.random=false;t.active(30000")
    end = cpp.index(' std::cout<<"FULL_ENDPOINT_SERVICE_PASS', start)
    cpp = cpp[:start] + """ for(unsigned n:{1U,64U,512U,896U,1460U}){
  Test t;t.random=false;t.active(30000,40000);uint32_t seq=t.peerNext;
  for(unsigned i=0;i<256;++i){std::vector<uint8_t> b(n,uint8_t(i));
   for(auto x:b)t.expectedRx.push_back(x);
   t.ingress.push_back(t.encode(seq+i*n,t.planned,24,65535,b));}
  t.peerNext+=256*n;t.until([&]{return t.expectedRx.empty();},400000);t.drain();
  std::vector<unsigned> ends;
  for(unsigned i=n-1;i<t.tcpRxByteTimes.size();i+=n)ends.push_back(t.tcpRxByteTimes[i]);
  unsigned wireBytes=std::max(60U,54+n)+24;
  double rx=rate(ends,wireBytes,"TCP_RX_PRODUCTION_CAPACITY",n);
  check(rx>=10.0,"production-capacity TCP RX service below 10G");
 }
""" + cpp[end:]
    (work / "rx-probe.cpp").write_text(cpp)
    model = work / "generated"
    model.mkdir()
    compiler = os.environ.get("GSIM_CXX", "clang++-19")
    commands = [
        (["mill", "-i", "netstack.runMain", "netstack.EmitProductionRxProbe", str(model)], work),
        ([str(gsim_source / "build/gsim/gsim"), "--threads=1", "--dir=" + str(model),
          str(model / "EthernetServiceHarness.fir")], ROOT),
        ([compiler, "-std=c++20", "-O1", "-g", "-fsanitize=address,undefined",
          "-fno-sanitize-recover=all", "-I" + str(model), "-I" + str(ROOT / "sim"),
          str(model / "EthernetServiceHarness0.cpp"), str(work / "rx-probe.cpp"),
          "-ldl", "-o", str(work / "rx-probe")], ROOT),
    ]
    print(work, flush=True)
    with (work / "build.log").open("w") as log:
        for command, cwd in commands:
            result = subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT)
            if result.returncode:
                raise RuntimeError("Probe build failed; inspect " + str(work / "build.log"))
    with (work / "result.log").open("w") as log:
        result = subprocess.run([str(work / "rx-probe")], cwd=ROOT,
                                stdout=log, stderr=subprocess.STDOUT)
    for name, expected in verified["source_sha256"].items():
        if digest(ROOT / name) != expected:
            raise RuntimeError("Authoritative sources changed during the isolated probe")
    files = ["source-verification.json", "src/main/scala/netstack/EthernetTcpHarness.scala",
             "src/main/scala/netstack/EmitProductionRxProbe.scala", "rx-probe.cpp",
             "generated/EthernetServiceHarness.fir", "result.log"]
    provenance = {
        "scope": "Isolated copy; RX ring changed from test 1KiB to production 64KiB; "
                 "other reduced harness parameters unchanged; no board throughput claim",
        "script_sha256": digest(Path(__file__).resolve()),
        "sha256": {name: digest(work / name) for name in files},
        "commands": [command for command, _ in commands],
        "exit_code": result.returncode,
    }
    (work / "probe-provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    print((work / "result.log").read_text(), end="")
    result.check_returncode()


if __name__ == "__main__":
    main()
