#!/usr/bin/env python3
"""Check that memory profiles alter attributes alone, including repeated export."""
from datetime import datetime, timezone
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
inputs = sorted((ROOT / "src").rglob("*.scala")) + [
    ROOT / n for n in ("build.mill", ".mill-version", ".mill-jvm-opts")]
before = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
work = ROOT / "build" / ("memory-profile-probe-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ"))
work.mkdir()
for source in inputs:
    dest = work / source.relative_to(ROOT)
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, dest)
probe = work / "src/main/scala/netstack/MemoryProfileProbe.scala"
probe.write_text("""package netstack
object MemoryProfileProbe extends App {
    for (profile <- Seq("hybrid", "bram", "auto")) {
        MemoryExport.profile = profile
        MemoryExport.emit(new TcpPayloadStore(), Array("--target-dir", s"build/$profile/store"))
        MemoryExport.emit(new MemoryQueueHarness, Array("--target-dir", s"build/$profile/queues"))
        if (profile == "hybrid") {
            MemoryExport.emit(new TcpPayloadStore(), Array("--target-dir", s"build/$profile/store"))
            MemoryExport.emit(new MemoryQueueHarness, Array("--target-dir", s"build/$profile/queues"))
        }
    }
}
""")
print(work, flush=True)
with (work / "elaboration.log").open("w") as log:
    subprocess.run(["mill", "-i", "netstack.runMain", "netstack.MemoryProfileProbe"],
                   cwd=work, stdout=log, stderr=subprocess.STDOUT, check=True)
normalized = {}
counts = {}
for profile in ["hybrid", "bram", "auto"]:
    files = sorted((work / "build" / profile).rglob("*.sv"))
    normalized[profile] = {
        str(p.relative_to(work / "build" / profile)): re.sub(r'\(\* ram_style = "(?:block|ultra)" \*\) ', "", p.read_text())
        for p in files}
    text = "\n".join(p.read_text() for p in files)
    counts[profile] = {kind: text.count('ram_style = "' + kind + '"') for kind in ["block", "ultra"]}
    if text.count("(* ram_style") != sum(counts[profile].values()):
        raise RuntimeError("Unexpected or duplicate RAM attribute")
if not normalized["hybrid"] == normalized["bram"] == normalized["auto"]:
    raise RuntimeError("Memory profile changed emitted logic")
if not (counts["hybrid"] == {"block": 3, "ultra": 1} and
        counts["bram"] == {"block": 4, "ultra": 0} and
        counts["auto"] == {"block": 0, "ultra": 0}):
    raise RuntimeError("Memory mapping did not select expected arrays: " + str(counts))
for p in inputs:
    if hashlib.sha256(p.read_bytes()).hexdigest() != before[str(p.relative_to(ROOT))]:
        raise RuntimeError("Source changed during probe")
record = {
    "source_sha256": before,
    "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    "profiles": counts,
    "logic_identical_after_attribute_removal": True,
    "physical_timing_verified": False,
    "rtl_sha256": {str(p.relative_to(work)): hashlib.sha256(p.read_bytes()).hexdigest()
                   for p in sorted((work / "build").rglob("*.sv"))},
}
(work / "provenance.json").write_text(json.dumps(record, indent=2) + "\n")
print("MEMORY_PROFILE_PASS hybrid/bram/auto logical RTL identical; repeated export idempotent")
