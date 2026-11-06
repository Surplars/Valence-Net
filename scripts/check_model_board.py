#!/usr/bin/env python3
"""Check network FIR logic across the simulation and board reset wrappers."""
import hashlib
import json
import pathlib
import re
import shutil

ROOT = pathlib.Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def modules(text):
    result = {}
    for chunk in re.split(r"(?=^  (?:public )?module )", text, flags=re.M)[1:]:
        name = re.match(r"  (?:public )?module ([^ :]+)", chunk)[1]
        chunk = re.sub(r"@\[[^\n]*?\]", "", chunk)
        chunk = re.sub(r"\bAsyncReset\b", "Reset", chunk)
        result[name] = re.sub(r"\s+", " ", chunk).strip()
    return result


def main():
    run = pathlib.Path((ROOT / "build/latest-vivado.txt").read_text().strip())
    model = ROOT / "build/gmii/gmii.fir"
    board = ROOT / "build/board/board.fir"
    a, b = modules(model.read_text()), modules(board.read_text())
    common = a.keys() & b.keys()
    different = sorted(n for n in common if a[n] != b[n])
    assert different == ["GmiiFrameRx"], different
    required = {"TcpTransmitQueue", "TcpReceiveRing", "TcpReceiveQueue",
                "TcpConnectionControl", "TcpCongestionControl", "Iperf3Application",
                "IperfStack", "MultiTcpIpv4Endpoint", "MultiEthernetTcpUdpEndpoint"}
    equal = sorted(common - set(different))
    assert required <= set(equal)
    assert len([n for n in equal if n.startswith("TcpPayloadStore")]) == 4
    for source in (ROOT / "build/board/rtl").glob("*.sv"):
        assert sha(source) == sha(run / "rtl" / source.name), source.name
    record = {
        "passed": True,
        "scope": "FIR logic text after source-location, whitespace and Reset/AsyncReset type normalization; not formal equivalence",
        "reset_boundary_tested_separately": True,
        "different_modules": different,
        "difference_reason": "Board GmiiFrameRx adds diagnostics ports; actual board RX MAC is independently tested",
        "equal_module_count": len(equal),
        "normalized_module_sha256": {n: hashlib.sha256(a[n].encode()).hexdigest() for n in equal},
        "model_fir_sha256": sha(model), "board_fir_sha256": sha(board),
        "frozen_rtl_sha256": {p.name: sha(p) for p in sorted((run / "rtl").glob("*.sv"))},
        "script_sha256": sha(pathlib.Path(__file__)), "run_directory": str(run),
    }
    (ROOT / "build/gmii-board-equivalence.json").write_text(json.dumps(record, indent=2) + "\n")
    shutil.copy2(board, run / "board.fir")
    print(f"PASS: {len(equal)} network FIR modules match after explicit reset-type/source-info normalization")


if __name__ == "__main__":
    main()
