#!/usr/bin/env python3
"""Compare the delivered TX32 model with the TX64/RX256K/ACK candidate."""
import concurrent.futures
import hashlib
import json
import pathlib
import re
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    out = ROOT / "build/transport-comparison"
    out.mkdir(exist_ok=True)
    archive = pathlib.Path((ROOT / "build/tx32-baseline.txt").read_text().strip())
    models = {32: archive / "gmii", 64: ROOT / "build/gmii"}
    driver = ROOT / "sim/gmii_burst.cpp"
    flags = ["clang++-19", "-std=c++20", "-O1", "-g", "-fsanitize=address,undefined",
             "-fno-sanitize-recover=all"]

    def compile_model(item):
        banks, model = item
        source = model / "IperfGmiiHarness0.cpp"
        obj = out / f"model-{banks}.o"
        marker = out / f"model-{banks}.sha256"
        digest = sha(source) + sha(model / "IperfGmiiHarness.h")
        baseline_obj = archive / "tx-window-comparison/model-32.o"
        if banks == 32 and not obj.exists() and baseline_obj.exists():
            recorded = json.loads((archive / "tx-window-comparison/verification.json").read_text())
            for name, expected in recorded["model_sha256"]["32"].items():
                assert sha(model / name) == expected
            shutil.copy2(baseline_obj, obj)
            marker.write_text(digest)
        with (out / f"compile-{banks}.log").open("w") as log:
            if not obj.exists() or not marker.exists() or marker.read_text() != digest:
                subprocess.run(flags + ["-I" + str(model), "-c", str(source), "-o", str(obj)],
                               stdout=log, stderr=subprocess.STDOUT, check=True)
                marker.write_text(digest)
            executable = out / f"probe-{banks}"
            subprocess.run(flags + ["-I" + str(model), str(obj), str(driver), "-ldl", "-o", str(executable)],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        return banks, executable

    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        executables = dict(pool.map(compile_model, models.items()))

    cases = []
    rates = {32: {}, 64: {}}
    ack_counts = {}
    def run(mode, banks, delay=None, expected_errors=None):
        name = f"{mode}-{banks}" + (f"-{delay}" if delay else "")
        command = [str(executables[banks]), mode, str(banks)] + ([str(delay)] if delay else [])
        result = subprocess.run(command, capture_output=True, text=True, timeout=360)
        log = out / (name + ".log")
        log.write_text(result.stdout + result.stderr)
        print(result.stdout + result.stderr, flush=True)
        result.check_returncode()
        assert "busy_drops=0" in result.stdout and "mac_drops=0" in result.stdout
        if mode == "window":
            rates[banks][delay] = float(re.search(r"model_mbps=([0-9.]+)", result.stdout)[1])
        if mode == "forward":
            ack_counts[banks] = int(re.search(r"ack_packets=(\d+)", result.stdout)[1])
            window = int(re.search(r"advertised_window_max=(\d+)", result.stdout)[1])
            assert window >= (256000 if banks == 64 else 63000)
        if expected_errors is not None:
            assert int(re.search(r"errors=(\d+)", result.stdout)[1]) == expected_errors
            assert "final_phase=1" in result.stdout
        cases.append({"mode": mode, "banks": banks, "delay_cycles": delay,
                      "expected_errors": expected_errors, "passed": True,
                      "log": log.name, "log_sha256": sha(log)})

    for delay in [32000, 64000, 80000]:
        for banks in [32, 64]:
            run("window", banks, delay)
    for banks in [32, 64]:
        run("forward", banks)
    run("reverse", 64)
    run("late-reset", 32, expected_errors=1)
    run("late-reset", 64, expected_errors=0)
    run("active-reset", 64, expected_errors=1)
    run("control-reset", 64, expected_errors=1)

    assert all(rate > 900 for rate in rates[64].values()), rates
    assert rates[64][64000] > rates[32][64000] * 1.15, rates
    assert rates[64][80000] > rates[32][80000] * 1.4, rates
    assert ack_counts[32] >= 1000 and ack_counts[64] <= 520, ack_counts
    record = {
        "passed": True, "scope": "Full MAC/TCP/iperf3 GSIM; external GMII stimuli and read-only observations",
        "clock_hz": 125000000, "measurement_cycles": 500000,
        "ack_latency_us": [256, 512, 640], "model_mbps": rates, "ack_counts_512_frames": ack_counts,
        "tx_banks": 64, "retained_bytes_at_mss1460": 93440, "rx_capacity_bytes": 262144,
        "congestion_cap_bytes": 131072, "cases": cases,
        "driver_sha256": sha(driver), "script_sha256": sha(pathlib.Path(__file__)),
        "model_sha256": {str(n): {p.name: sha(p) for p in [d / "gmii.fir", d / "IperfGmiiHarness.h",
                                                           d / "IperfGmiiHarness0.cpp"]}
                         for n, d in models.items()},
        "board_throughput_measured": False,
    }
    (out / "verification.json").write_text(json.dumps(record, indent=2) + "\n")
    print("PASS: 13 transport cases; simulation throughput is not board throughput")


if __name__ == "__main__":
    main()
