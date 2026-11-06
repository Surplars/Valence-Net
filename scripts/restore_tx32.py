#!/usr/bin/env python3
"""Restore the hash-checked historical TX32 comparison model without old paths."""
import hashlib
import json
from pathlib import Path
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def restore():
    fixture = ROOT / "sim/fixtures"
    manifest = json.loads((fixture / "tx32-model.json").read_text())
    archive = fixture / "tx32-model.tar.xz"
    if hashlib.sha256(archive.read_bytes()).hexdigest() != manifest["archive_sha256"]:
        raise RuntimeError("TX32 archive checksum mismatch")
    target = ROOT / "build/tx32-baseline-restored"
    expected = manifest["files_sha256"]
    with tarfile.open(archive, "r:xz") as packed:
        members = packed.getmembers()
        if {m.name for m in members} != set(expected) or len(members) != len(expected):
            raise RuntimeError("Unexpected archive members")
        verified = {}
        for member in members:
            path = Path(member.name)
            if not member.isfile() or path.is_absolute() or ".." in path.parts:
                raise RuntimeError("Unsafe archive member: " + member.name)
            data = packed.extractfile(member).read()
            if hashlib.sha256(data).hexdigest() != expected[member.name]:
                raise RuntimeError("TX32 model checksum mismatch: " + member.name)
            output = target / path
            if output.exists() and output.read_bytes() != data:
                raise RuntimeError("Preserving changed existing model: " + str(output))
            verified[path] = data
        for path, data in verified.items():
            output = target / path
            output.parent.mkdir(parents=True, exist_ok=True)
            if not output.exists():
                output.write_bytes(data)
    pointer = ROOT / "build/tx32-baseline.txt"
    pointer.write_text(str(target) + "\n")
    return target


if __name__ == "__main__":
    print(restore())
