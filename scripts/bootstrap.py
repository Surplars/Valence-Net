#!/usr/bin/env python3
"""Restore pinned sources without overwriting an existing checkout."""
import argparse,json,pathlib,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument("--ssh",action="store_true");a=p.parse_args()
pins=json.loads((ROOT/"dependencies.json").read_text())
for name in ["valence","valence-net"]:
    pin=pins[name];dest=ROOT/"deps"/name
    if not dest.exists():
        url=pin["url"]
        if a.ssh:url=url.replace("https://github.com/","git@github.com:")
        dest.parent.mkdir(exist_ok=True)
        subprocess.run(["git","clone","--no-checkout",url,str(dest)],check=True)
        subprocess.run(["git","-C",str(dest),"checkout","--detach",pin["commit"]],check=True)
    head=subprocess.check_output(["git","-C",str(dest),"rev-parse","HEAD"],text=True).strip()
    dirty=subprocess.check_output(["git","-C",str(dest),"status","--porcelain"],text=True)
    if head!=pin["commit"] or dirty:raise RuntimeError(str(dest)+": unexpected revision or local changes; preserved")
    print(name,head)
