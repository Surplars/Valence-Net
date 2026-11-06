#!/usr/bin/env python3
"""Restore pinned sources; optionally clone the reproducible local test tools."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def checkout(name, pin, destination, ssh, ignore_untracked=False):
    if not destination.exists():
        url = pin["url"]
        if ssh:
            url = url.replace("https://github.com/", "git@github.com:")
        destination.mkdir(parents=True)
        subprocess.run(["git", "init", "--quiet", str(destination)], check=True)
        subprocess.run(["git", "-C", str(destination), "remote", "add", "origin", url], check=True)
        # Historical branches may have been removed; fetch the exact locked object.
        subprocess.run(["git", "-C", str(destination), "fetch", "--depth=1",
                        "origin", pin["commit"]], check=True)
        subprocess.run(["git", "-C", str(destination), "checkout", "--detach", pin["commit"]], check=True)
    head = subprocess.check_output(
        ["git", "-C", str(destination), "rev-parse", "HEAD"], text=True).strip()
    args = ["git", "-C", str(destination), "status", "--porcelain"]
    if ignore_untracked:
        args.append("--untracked-files=no")
    dirty = subprocess.check_output(args, text=True)
    if head != pin["commit"] or dirty:
        raise RuntimeError(str(destination) + ": unexpected revision or local changes; preserved")
    print(name, head)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ssh", action="store_true")
    parser.add_argument("--tools", action="store_true", help="also clone pinned iperf3 and GSIM sources")
    args = parser.parse_args()
    pins = json.loads((ROOT / "dependencies.json").read_text())
    for name in ["valence", "valence-net"]:
        checkout(name, pins[name], ROOT / "deps" / name, args.ssh)
    if args.tools:
        if (ROOT / "tools").is_symlink():
            raise RuntimeError("tools is a borrowed local symlink; use a fresh checkout for restoration")
        checkout("iperf3", pins["iperf3"], ROOT / "tools/iperf3-src", args.ssh, True)
        gsim = dict(pins["gsim"], url="https://github.com/OpenXiangShan/gsim.git")
        destination = Path(os.environ.get(
            "GSIM_SOURCE", str(Path.home() / ".cache/valence-net/gsim-src-93b8cd23")))
        checkout("gsim", gsim, destination, args.ssh, True)
        print("Tool sources restored; build them as described in RESTORE.md.")


if __name__ == "__main__":
    main()
