#!/usr/bin/env python3
"""Elaborate boundary/non-power-of-two connection counts in a native copy."""
from datetime import datetime, timezone
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("counts", nargs="*", type=int, default=[1, 3, 8])
args = parser.parse_args()
if not args.counts or any(n < 1 or n > 8 for n in args.counts):
    parser.error("connection counts must be in 1..8")
counts = list(dict.fromkeys(args.counts))
inputs = sorted((ROOT / "src").rglob("*.scala")) + [
    ROOT / n for n in ("build.mill", ".mill-version", ".mill-jvm-opts")]
def hashes():
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
before = hashes()
work = ROOT / "build" / ("configuration-probe-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ"))
work.mkdir(parents=True, exist_ok=False)
for source in inputs:
    destination = work / source.relative_to(ROOT)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    if hashlib.sha256(destination.read_bytes()).hexdigest() != before[str(source.relative_to(ROOT))]:
        raise RuntimeError("Source changed while copying: " + str(source))
probe = work / "src/main/scala/netstack/MultiConfigurationProbe.scala"
probe.write_text("""package netstack
import circt.stage.ChiselStage
object MultiConfigurationProbe extends App {
    for (count <- Seq(""" + ",".join(map(str, counts)) + """)) {
        println(s"CONFIG_ELABORATE connections=$count")
        ChiselStage.emitSystemVerilogFile(new MultiEthernetTcpUdpEndpoint(connectionCount = count),
            Array("--target-dir", s"build/config-$count"))
        println(s"CONFIG_PASS connections=$count")
    }
}
""")
print(work, flush=True)
with (work / "elaboration.log").open("w") as log:
    result = subprocess.run(["mill", "-i", "netstack.runMain", "netstack.MultiConfigurationProbe"],
                            cwd=work, stdout=log, stderr=subprocess.STDOUT)
log = (work / "elaboration.log").read_text()
if result.returncode:
    print(log[-12000:])
    result.check_returncode()
for count in counts:
    if f"CONFIG_PASS connections={count}" not in log:
        raise RuntimeError("Missing elaboration result")
if hashes() != before:
    raise RuntimeError("Source changed during elaboration; rerun for current evidence")
record = {
    "source_sha256": before,
    "probe_generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    "connection_counts": counts,
    "elaborated": True,
    "functional_verification": False,
    "physical_timing_verification": False,
    "elaboration_log": str(work / "elaboration.log"),
    "warnings": [line for line in log.splitlines() if "[warn]" in line],
    "rtl_sha256": {str(p.relative_to(work)): hashlib.sha256(p.read_bytes()).hexdigest()
                   for p in sorted((work / "build").rglob("*.sv"))},
}
(work / "provenance.json").write_text(json.dumps(record, indent=2) + "\n")
print(json.dumps({"counts": counts, "passed": True, "warnings": len(record["warnings"])}))
