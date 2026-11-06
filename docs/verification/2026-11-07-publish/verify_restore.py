from pathlib import Path
import hashlib
import json
import os
import subprocess
import traceback

ROOT = Path(__file__).resolve().parent
REMOTE = "git@github.com:Surplars/Valence-Net.git"
record = {"scope": "Remote-only clone, exact dependency/tool source restore and compile smoke",
          "physical_timing_verified": False, "board_throughput_measured": False, "steps": []}


def run(command, cwd=ROOT, env=None):
    print("RUN", *map(str, command), flush=True)
    subprocess.run(list(map(str, command)), cwd=cwd, env=env, check=True)
    record["steps"].append({"command": list(map(str, command)), "cwd": str(cwd), "passed": True})


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


try:
    for branch in ["n57b-main", "n57b-tested"]:
        dest = ROOT / ("remote-" + branch)
        run(["git", "clone", "--depth=1", "--single-branch", "--branch", branch, REMOTE, dest])
        run(["python3", "scripts/restore_tx32.py"], dest)
        run(["python3", "scripts/restore_tx32.py"], dest)
        if branch == "n57b-tested":
            files = json.loads((dest / "docs/preserved-verification/original-source-sha256.json").read_text())
            assert len(files) == 81
            assert all(sha(dest / name) == expected for name, expected in files.items())
            record["original_source_files_verified"] = len(files)
        record[branch] = {"commit": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=dest, text=True).strip(),
            "restored_model_sha256": {
                str(p.relative_to(dest / "build/tx32-baseline-restored")): sha(p)
                for p in sorted((dest / "build/tx32-baseline-restored/gmii").iterdir())}}
    main = ROOT / "remote-n57b-main"
    env = dict(os.environ, GSIM_SOURCE=str(ROOT / "remote-gsim"))
    run(["python3", "scripts/bootstrap.py", "--ssh", "--tools"], main, env)
    run(["python3", "scripts/bootstrap.py", "--ssh", "--tools"], main, env)
    run(["mill", "-i", "hw.compile"], main, env)
    record["dependency_commits"] = {
        name: subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=directory, text=True).strip()
        for name, directory in {
            "valence": main / "deps/valence", "valence-net": main / "deps/valence-net",
            "iperf3": main / "tools/iperf3-src", "gsim": ROOT / "remote-gsim"}.items()}
    pins = json.loads((main / "dependencies.json").read_text())
    assert all(record["dependency_commits"][name] == pins[name]["commit"]
               for name in record["dependency_commits"])
    assert not subprocess.check_output(["git", "status", "--porcelain"], cwd=main, text=True)
    record["passed"] = True
    print("REMOTE_RESTORE_PASS", flush=True)
except Exception:
    record["passed"] = False
    traceback.print_exc()
    raise
finally:
    (ROOT / "remote-restore-verification.json").write_text(json.dumps(record, indent=2) + "\n")
