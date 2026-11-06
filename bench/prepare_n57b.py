#!/usr/bin/env python3
"""Make a separate N57B application using the current Valence-Net sources.

No original source/build artifact is modified. This is a source migration,
not a transfer of N57B's board or timing qualification.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = "57c698496c55f3afe6c882f7a8ab9fb041f6b66b"
OVERRIDES = {
    "TcpReceiveQueue.scala": "f6d7cc6ac5755e55721e049dbd63aff744fb6e5fc15ae24d4b73b6efb23fbd5d",
    "TcpReceiveRing.scala": "dbabb30de18fa8744eb9131faa58c760c55ce1711399f19859e292d062c85d83",
}
FOLDERS = ["src", "sim", "scripts", "fpga"]
FILES = ["build.mill", ".mill-version", ".mill-jvm-opts", ".gitignore", "Makefile",
         "dependencies.json", "README.md", "VALIDATION.md", "CANDIDATE.json"]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_hashes(root):
    files = [root / name for name in FILES]
    files += [p for name in FOLDERS for p in (root / name).rglob("*")
              if p.is_file() and "__pycache__" not in p.parts]
    return {str(p.relative_to(root)): sha(p) for p in sorted(files)}


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()


def replace(path, before, after):
    text = path.read_text()
    if text.count(before) != 1:
        raise RuntimeError("Review changed migration input: " + str(path))
    path.write_text(text.replace(before, after))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    if output.exists() or output == source or source in output.parents:
        raise RuntimeError("Output must be a new independent directory")
    if str(output).startswith("/mnt/"):
        raise RuntimeError("Keep the source/Git/build workspace on native WSL storage")
    before = source_hashes(source)
    pins = json.loads((source / "dependencies.json").read_text())
    if pins["valence-net"]["commit"] != BASE:
        raise RuntimeError("This migration targets the N57B 57c6984 dependency")
    for name, expected in OVERRIDES.items():
        if sha(source / "src/main/scala/netstack" / name) != expected:
            raise RuntimeError("Review changed N57B override: " + name)
    for name in ["valence", "valence-net"]:
        dep = source / "deps" / name
        if git(dep, "rev-parse", "HEAD") != pins[name]["commit"] or git(dep, "status", "--porcelain"):
            raise RuntimeError("Original dependency must match its clean pin: " + name)
    net_head = git(ROOT, "rev-parse", "HEAD")
    output.mkdir(parents=True)
    for name in FOLDERS:
        shutil.copytree(source / name, output / name, ignore=shutil.ignore_patterns("__pycache__"))
    for name in FILES:
        shutil.copy2(source / name, output / name)
    archive = output / "docs/n57b-original"
    archive.mkdir(parents=True)
    for name in ["README.md", "VALIDATION.md", "CANDIDATE.json", "dependencies.json"]:
        shutil.copy2(source / name, archive / name)
    for name in OVERRIDES:
        (output / "src/main/scala/netstack" / name).unlink()
    replace(output / "build.mill",
            'p.ext == "scala" &&\n            !Set("TcpReceiveQueue.scala", "TcpReceiveRing.scala").contains(p.last)',
            'p.ext == "scala"')
    replace(output / "src/main/scala/iperf/IperfStack.scala",
            "txBanks = txBanks, rxWords = 16384)",
            "txBanks = txBanks, rxWords = 16384, pressureWindowUpdates = true)")
    replace(output / "src/main/scala/iperf/ReceiveWindowHarness.scala",
            "new TcpReceiveQueue(16384)", "new TcpReceiveQueue(16384, pressureWindowUpdates = true)")
    # Use the current BRAM/URAM export preferences on the ZU15EG board.
    replace(output / "src/main/scala/iperf/Emit.scala",
            'ChiselStage.emitSystemVerilogFile(generate, Array("--target-dir", out.resolve("rtl").toString),\n'
            '        Array("-disable-all-randomization", "-strip-debug-info"))',
            'netstack.MemoryExport.emit(generate, Array("--target-dir", out.resolve("rtl").toString))')
    for name, origin, commit in [
        ("valence", source / "deps/valence", pins["valence"]["commit"]),
        ("valence-net", ROOT, net_head),
    ]:
        dest = output / "deps" / name
        dest.parent.mkdir(exist_ok=True)
        subprocess.run(["git", "clone", "--no-checkout", str(origin), str(dest)], check=True)
        subprocess.run(["git", "-C", str(dest), "checkout", "--detach", commit], check=True)
        subprocess.run(["git", "-C", str(dest), "remote", "set-url", "origin", pins[name]["url"]], check=True)
    net_files = sorted((ROOT / "src").rglob("*.scala")) + [ROOT / "build.mill"]
    net_sha = {str(p.relative_to(ROOT)): sha(p) for p in net_files}
    for name in net_sha:
        dest = output / "deps/valence-net" / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, dest)
    pins["valence-net"]["commit"] = net_head
    (output / "dependencies.json").write_text(json.dumps(pins, indent=2) + "\n")
    # A different UART label prevents a future rebuild masquerading as N57B.
    for folder in ["fpga", "scripts", "sim"]:
        for path in (output / folder).rglob("*"):
            if path.is_file() and path.suffix in {".py", ".ps1", ".sv"}:
                text = path.read_text()
                if "TX64-RX256K-N57B" in text:
                    path.write_text(text.replace("TX64-RX256K-N57B", "TX64-RX256K-N57M"))
    replace(output / "fpga/iperf_uart_log.sv",
            "8'd99:header_char=8'h42;", "8'd99:header_char=8'h4d;")
    (output / "tools").symlink_to(source / "tools", target_is_directory=True)
    (output / "build").mkdir()
    # The transport comparison explicitly distinguishes this historical model.
    shutil.copy2(source / "build/tx32-baseline.txt", output / "build/tx32-baseline.txt")
    record = {
        "status": "source_merge_candidate", "build_tag": "TX64-RX256K-N57M",
        "original_project": str(source), "original_net_commit": BASE,
        "net_commit": net_head, "net_worktree_dirty": bool(git(output / "deps/valence-net", "status", "--porcelain")),
        "net_source_sha256": net_sha, "original_project_sha256": before,
        "parameters": {"connections": 2, "tx_banks": 64, "rx_words": 16384,
                       "rx_bytes": 262144, "window_scale": 3, "pressure_window_updates": True},
        "physical_sealed": False, "board_programmed_by_agent": False,
        "board_iperf_measured": False, "migration_script_sha256": sha(Path(__file__)),
    }
    (output / "CANDIDATE.json").write_text(json.dumps(record, indent=2) + "\n")
    readme = """# N57M: N57B application on current Valence-Net

Separate source migration; the original N57B workspace and bitstream are preserved.
Two TCP contexts, 64 TX banks, 256 KiB RX/context, window scale 3, and the N57B
pressure-window ACK policy are retained. Local protocol-stack overrides are removed.
The current shared receive frontend and selective memory queues are used.

This is a 1G GMII board integration. New timing, bitstream and board throughput
are unverified. Historical N57B records are in docs/n57b-original and do not
qualify this source. CANDIDATE.json records exact dependency source hashes;
if net_worktree_dirty is true the commit alone does not identify the source.
The tools symlink and old TX32 comparison model are local test dependencies.

Run python3 sim/build.py gmii, python3 sim/build.py rxqueue,
build/rxqueue/test and python3 sim/test_transport.py in native WSL.
The merged Valence-Net report describes the tests actually completed.
"""
    (output / "README.md").write_text(readme)
    (output / "VALIDATION.md").write_text("# Migration validation\n\nSee the current Valence-Net N57B merge report.\n"
        "No new physical or board qualification has been transferred.\n")
    if source_hashes(source) != before:
        raise RuntimeError("Original N57B source changed during migration")
    print(json.dumps({"output": str(output), "net_commit": net_head,
                      "dirty_snapshot": record["net_worktree_dirty"], "original_unchanged": True}))


if __name__ == "__main__":
    main()
