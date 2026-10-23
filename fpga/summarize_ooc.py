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
# Coverage/route metrics do not silently erase tool-log constraint warnings.
log_path = root / "vivado.log"
if log_path.exists():
    messages = re.findall(r"^CRITICAL WARNING: (.+)$", log_path.read_text(errors="replace"), re.MULTILINE)
    report["tool_log_critical_warnings"] = [{"message": message, "occurrences": messages.count(message)}
                                           for message in dict.fromkeys(messages)]
    errors = re.findall(r"ERROR: ([^\r\n]+)", log_path.read_text(errors="replace"))
    report["tool_log_errors"] = list(dict.fromkeys(errors))
    console_path = root / "console.log"
    console_errors = re.findall(r"ERROR: ([^\r\n]+)", console_path.read_text(errors="replace")) if console_path.exists() else []
    report["native_console_errors"] = list(dict.fromkeys(console_errors))
    report["tool_log_review_required"] = bool(messages or errors or console_errors)
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
    if rows[top]["status"] == "SYNTH_ONLY":
        report["modules"][top] = {
            "stage": "synthesized",
            "setup_wns_ns": float(rows[top]["setup_wns_ns"]),
            "hold_whs_ns": float(rows[top]["hold_whs_ns"]),
            "blackboxes": int(rows[top]["blackboxes"]),
            "routed_timing_verified": False,
        }
        continue
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
    core_name = provenance.get("core_module", "EthernetTcpUdpEndpoint")
    data["core_module"] = core_name if top == "ValenceNetTimingTop" else top
    core_row = re.search(r"^\|\s*core\s*\|\s*" + re.escape(core_name) + r"\s*\|(.+)\|$", utilization, re.MULTILINE)
    if core_row:
        core_resources = [int(x.strip()) for x in core_row.group(1).split("|")]
        data["core_resources"] = dict(zip(data["resources"], core_resources))
    # Inspect actual route counts; route_design returning is insufficient.
    route = (folder / "post_route_status.rpt").read_text(errors="replace")
    route_counts = {}
    for name, label in [("routable_nets", "routable nets"),
                        ("fully_routed_nets", "fully routed nets"),
                        ("routing_errors", "nets with routing errors")]:
        match = re.search(r"# of " + re.escape(label) + r"\.+\s*:\s*(\d+)", route)
        if not match:
            raise RuntimeError("Missing routing count: " + label)
        route_counts[name] = int(match.group(1))
    data["routing"] = route_counts
    data["routing_clean"] = (route_counts["routing_errors"] == 0 and
                             route_counts["routable_nets"] == route_counts["fully_routed_nets"])
    for label, filename in [("drc", "post_route_drc.rpt"),
                            ("methodology", "post_route_methodology.rpt")]:
        text = (folder / filename).read_text(errors="replace")
        rules = re.findall(r"^\|\s*([A-Z][A-Z0-9_-]*-\d+)\s*\|\s*([^|]+?)\s*\|[^|]*\|\s*(\d+)\s*\|", text, re.MULTILINE)
        data[label + "_rules"] = [{"rule": rule, "severity": severity.strip(), "count": int(count)}
                                  for rule, severity, count in rules]
        data[label + "_errors"] = sum(int(count) for _, severity, count in rules
                                       if "error" in severity.lower())
    # Keep raw reports available for detailed advisory review.
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
    data["routed_timing_verified"] = (data["numeric_timing_met"] and
                                       data["timing_coverage_clean"] and
                                       data["routing_clean"] and data["blackboxes"] == 0 and
                                       data["drc_errors"] == 0)
    data["stage"] = "routed"
    report["modules"][top] = data
output = root / ("summary-partial.json" if progress != "completed" else "summary.json")
output.write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
