#!/usr/bin/env python3
"""Freeze the full endpoint with real registered timing boundaries and a BUFG.

The shell is ONLY a static-timing load/stimulus fixture. Functional packet tests
exercise the protocol RTL independently. It is not a MAC, driver, or board top.
All core inputs have independent retained stimulus registers; all core outputs
have retained capture registers. No core data path is excepted from timing.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CORE = "MultiEthernetTcpUdpEndpoint"
TOP = "ValenceNetTimingTop"

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def ports(source):
    text = re.sub(r"//[^\n]*", "", source)
    match = re.search(r"module\s+" + CORE + r"\s*\((.*?)\);", text, re.S)
    if not match:
        raise RuntimeError("Could not parse emitted top ports")
    direction, width = None, 1
    result = []
    for entry in match.group(1).split(","):
        field = re.fullmatch(r"\s*(?:(input|output)\s+)?(?:wire\s+|logic\s+|reg\s+)?(?:\[(\d+):0\]\s*)?(\w+)\s*", entry)
        if not field:
            raise RuntimeError("Unexpected emitted port declaration: " + entry)
        declared, high, name = field.groups()
        if declared:
            direction = declared
            width = int(high) + 1 if high else 1
        elif high:
            width = int(high) + 1
        if direction is None:
            raise RuntimeError("Missing port direction")
        result.append((direction, width, name))
    if len(set(p[2] for p in result)) != len(result):
        raise RuntimeError("Duplicate port")
    assert ("input", 1, "clock") in result and ("input", 1, "reset") in result
    return result

def shell(spec):
    lines = [
        "// Static-timing fixture: random stimuli intentionally are not a protocol test.",
        "// Real register boundaries; source core and RAM geometry are unchanged.",
        "module " + TOP + "(input wire clock, output reg [63:0] signature = 64'b0);",
        "wire core_clock;",
        '(* DONT_TOUCH = "true" *) BUFGCE coreClockBuffer(.I(clock), .CE(1\'b1), .O(core_clock));',
        '(* DONT_TOUCH = "true" *) reg [3:0] reset_count = 0;',
        "always @(posedge core_clock) if (reset_count != 15) reset_count <= reset_count + 1'b1;",
        "wire core_reset = reset_count < 8;",
    ]
    bindings, samples = [], []
    for direction, width, name in spec:
        if name == "clock":
            bindings.append(".clock(core_clock)")
            continue
        if name == "reset":
            bindings.append(".reset(core_reset)")
            continue
        shape = "" if width == 1 else f"[{width-1}:0] "
        if direction == "input":
            seed = int(hashlib.sha256(name.encode()).hexdigest(), 16) & ((1 << width) - 1)
            seed = seed or 1
            lines.append(f'(* DONT_TOUCH = "true", KEEP = "true" *) reg {shape}stim_{name} = {width}\'h{seed:x};')
            expression = f"~stim_{name}" if width == 1 else (
                f"{{stim_{name}[{width-2}:0], stim_{name}[{width-1}] ^ stim_{name}[{max(0,width//2-1)}]}}")
            lines.append(f"always @(posedge core_clock) stim_{name} <= {expression};")
            bindings.append(f".{name}(stim_{name})")
        else:
            lines.append(f"wire {shape}result_{name};")
            lines.append(f'(* DONT_TOUCH = "true", KEEP = "true" *) reg {shape}sample_{name} = 0;')
            lines.append(f"always @(posedge core_clock) sample_{name} <= result_{name};")
            samples.append("sample_" + name)
            bindings.append(f".{name}(result_{name})")
    lines.append('(* DONT_TOUCH = "true", KEEP_HIERARCHY = "true" *) ' + CORE + " core(\n    " + ",\n    ".join(bindings) + "\n);")
    groups = [samples[i::64] for i in range(64)]
    expressions = [("^{" + ", ".join(group) + "}") if group else "1'b0" for group in groups]
    lines.append("always @(posedge core_clock) signature <= {" + ", ".join(reversed(expressions)) + "};")
    lines.append("endmodule")
    return "\n".join(lines) + "\n"

def main():
    global CORE
    parser = argparse.ArgumentParser()
    parser.add_argument("--core", choices=["EthernetTcpUdpEndpoint", "MultiEthernetTcpUdpEndpoint"],
                        default=CORE, help="full production core; both use retained register boundaries")
    CORE = parser.parse_args().core
    result = subprocess.run([sys.executable, str(ROOT / "fpga/prepare_ooc.py"), "--reemit-check", CORE],
                            cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        print(result.stdout, end="", file=sys.stderr)
        print(result.stderr, end="", file=sys.stderr)
        result.check_returncode()
    run = Path(result.stdout.splitlines()[0])
    old = run / "input" / CORE
    destination = run / "input" / TOP
    old.rename(destination)
    specification = ports((destination / (CORE + ".sv")).read_text())
    wrapper = destination / (TOP + ".sv")
    wrapper.write_text(shell(specification))
    (run / "targets.txt").write_text(TOP + "\n")
    provenance = json.loads((run / "provenance.json").read_text())
    provenance["targets"] = {TOP: "registered-boundary full default " + CORE}
    provenance["core_module"] = CORE
    provenance["rtl_sha256"] = {str(p.relative_to(run)): digest(p) for p in sorted(destination.iterdir())
                                 if p.suffix in {".sv", ".v"}}
    provenance["scope"] = "full default " + CORE + " with physical BUFG and retained input/output registers; no board or MAC signoff"
    provenance["timing_shell"] = {
        "generator_sha256": digest(Path(__file__)),
        "wrapper_sha256": digest(wrapper),
        "stimulus_bits": sum(w for d, w, n in specification if d == "input" and n not in {"clock", "reset"}),
        "capture_bits": sum(w for d, w, n in specification if d == "output"),
        "functional_test": False,
        "core_unchanged": True,
        "clock_buffer_site": "BUFGCE_X0Y0",
        "boundary": "one real clock domain; no artificial data input ports",
    }
    (run / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    print(run)
    print("//wsl.localhost/Ubuntu-24.04" + str(run))

if __name__ == "__main__":
    main()
