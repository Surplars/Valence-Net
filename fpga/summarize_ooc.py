#!/usr/bin/env python3
"""Audit frozen RTL plus routed OOC reports; never infer board timing/Fmax."""
from pathlib import Path
import argparse
import csv
import hashlib
import json
import re

parser = argparse.ArgumentParser()
parser.add_argument("run", type=Path)
parser.add_argument("--partial", action="store_true", help="inspect completed modules while the batch remains active")
args = parser.parse_args()
root = args.run.resolve()
provenance = json.loads((root / "provenance.json").read_text())
for name, expected in provenance["rtl_sha256"].items():
    if hashlib.sha256((root / name).read_bytes()).hexdigest() != expected:
        raise RuntimeError("Frozen RTL changed: " + name)
if hashlib.sha256((root / "ooc.tcl").read_bytes()).hexdigest() != provenance["script_sha256"]:
    raise RuntimeError("Timing script changed after snapshot")
progress = (root / "progress.txt").read_text().strip()
if progress != "completed" and not args.partial:
    raise RuntimeError("Run is still incomplete: " + progress)
with (root / "results.tsv").open() as stream:
    rows = {row["top"]: row for row in csv.DictReader(stream, delimiter="\t")}
report = {
    "scope": provenance["scope"],
    "part": provenance["part"],
    "period_ns": provenance["period_ns"],
    "progress": progress,
    "frozen_rtl_hashes_verified": True,
    "modules": {},
    "integrated_soc_timing_verified": False,
}
keys = ["setup_wns_ns", "setup_tns_ns", "setup_failing_endpoints", "setup_endpoints",
        "hold_whs_ns", "hold_ths_ns", "hold_failing_endpoints", "hold_endpoints",
        "pulse_width_slack_ns", "pulse_width_total_slack_ns", "pulse_width_failing_endpoints", "pulse_width_endpoints"]
required_checks = {"no_clock", "constant_clock", "pulse_width_clock", "unconstrained_internal_endpoints",
                   "no_input_delay", "no_output_delay", "multiple_clock", "generated_clocks", "loops",
                   "partial_input_delay", "partial_output_delay", "latch_loops"}
for top in provenance["targets"]:
    if top not in rows:
        report["modules"][top] = {"stage": "incomplete"}
        continue
    folder = root / top
    timing = (folder / "post_route_timing.rpt").read_text(errors="replace")
    summary = timing.split("| Design Timing Summary", 1)[1]
    numbers = None
    for line in summary.splitlines():
        fields = line.split()
        if len(fields) == 12 and re.fullmatch(r"-?\d+\.\d+", fields[0]):
            numbers = list(map(float, fields))
            break
    if numbers is None:
        raise RuntimeError("Unrecognized timing summary: " + top)
    data = dict(zip(keys, numbers))
    checks = {name: int(count) for name, count in re.findall(r"checking (\w+) \((\d+)\)", timing)}
    if not required_checks <= checks.keys():
        raise RuntimeError("Missing timing coverage checks: " + top)
    data["coverage_checks"] = checks
    data["register_setup_ns"] = float(rows[top]["register_setup_ns"])
    data["register_hold_ns"] = float(rows[top]["register_hold_ns"])
    data["blackboxes"] = int(rows[top]["blackboxes"])
    utilization = (folder / "post_route_utilization.rpt").read_text(errors="replace")
    pattern = r"^\|\s*" + re.escape(top) + r"\s*\|\s*\(top\)\s*\|(.+)\|$"
    match = re.search(pattern, utilization, re.MULTILINE)
    if not match:
        raise RuntimeError("Missing total utilization: " + top)
    resources = [int(x.strip()) for x in match.group(1).split("|")]
    data["resources"] = dict(zip(["total_luts", "logic_luts", "lutram", "srl", "ffs", "ramb36", "ramb18", "uram", "dsps"], resources))
    # Raw route/DRC files remain authoritative; save their text for explicit
    # inspection instead of equating successful route_design with clean routing.
    data["route_status_report"] = str(folder / "post_route_status.rpt")
    data["drc_report"] = str(folder / "post_route_drc.rpt")
    data["methodology_report"] = str(folder / "post_route_methodology.rpt")
    setup = (folder / "post_route_setup_paths.rpt").read_text(errors="replace")
    registers = (folder / "post_route_register_paths.rpt").read_text(errors="replace")
    for label, text in [("worst_path", setup), ("worst_register_path", registers)]:
        source = re.search(r"^\s*Source:\s*(.+)", text, re.MULTILINE)
        dest = re.search(r"^\s*Destination:\s*(.+)", text, re.MULTILINE)
        levels = re.search(r"^\s*Logic Levels:\s*(.+)", text, re.MULTILINE)
        data[label] = {"source": source.group(1).strip() if source else None,
                       "destination": dest.group(1).strip() if dest else None,
                       "logic_levels": levels.group(1).strip() if levels else None}
    data["numeric_timing_met"] = all(data[k] >= 0 for k in ["setup_wns_ns", "hold_whs_ns", "pulse_width_slack_ns"])
    data["timing_coverage_clean"] = all(checks[k] == 0 for k in required_checks)
    data["stage"] = "routed"
    report["modules"][top] = data
output = root / ("summary-partial.json" if progress != "completed" else "summary.json")
output.write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
