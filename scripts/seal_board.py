#!/usr/bin/env python3
"""Record the real Xilinx board-boundary test, separate from network simulation."""
import argparse, datetime, hashlib, json, pathlib, re, shutil
ROOT = pathlib.Path(__file__).resolve().parents[1]
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("test_directory", type=pathlib.Path)
    args = ap.parse_args()
    test = args.test_directory.resolve()
    raw = (test / "test.log").read_bytes()
    log = raw.decode("utf-16") if raw.startswith((b"\xff\xfe", b"\xfe\xff")) else raw.decode()
    expected = ("BOARD_RESET_PASS releases=2 cycles_each=1250000 TX125=PASS "
                "DDR250_positive_only=PASS TX_bytes=64 RX_bytes=64 "
                "RX_stopped=PASS calibration_loss=PASS")
    if expected not in log or re.search(r"Fatal:|Error:|BOARD_RESET_FAIL", log):
        raise RuntimeError("Board boundary simulation did not pass")
    sources = ["iperf_board.sv", "iperf_uart_log.sv", "native_rgmii.sv", "native_gmac_clocks.sv",
               "native_gmac_divided_clock.sv"]
    for name in sources:
        if sha(test / name) != sha(test.parent / name):
            raise RuntimeError("Test source differs from frozen run: " + name)
    if sha(test / "iperf_board.sv") != sha(ROOT / "fpga/iperf_board.sv"):
        raise RuntimeError("Current board source differs from tested source")
    if sha(test / "iperf_uart_log.sv") != sha(ROOT / "fpga/iperf_uart_log.sv"):
        raise RuntimeError("Current UART source differs from tested source")
    if sha(test / "board_reset_tb.sv") != sha(ROOT / "sim/board_reset_tb.sv"):
        raise RuntimeError("Current testbench differs from tested source")
    phase_match=re.search(r"BOARD_RX_PHASE degrees=([0-9.]+)",log)
    if not phase_match or float(phase_match[1])!=45.0:
        raise RuntimeError("Corrected RX capture phase not verified by primitive oracle")
    data = {
        "sealed_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "scope": "Actual board RTL and Xilinx clock/delay/DDR primitives; network core is a port stub",
        "run_directory": str(test), "passed": True,
        "release_cycles_each": 1250000, "release_count": 2,
        "tx_clock_hz": 125000000, "ddr_clock_hz": 250000000,
        "rx_mmcm_phase_degrees": float(phase_match[1]),
        "tx_pad_data_positive_edges_only": True,
        "tx_bytes_checked": 64, "rx_bytes_checked": 64,
        "stopped_rx_reset": True, "calibration_loss_recovery": True,
        "input_sha256": {name: sha(test / name) for name in sources + ["board_reset_tb.sv", "glbl.v"]},
        "test_log_sha256": sha(test / "test.log"),
    }
    target = ROOT / "build/board-reset"
    target.mkdir(parents=True, exist_ok=True)
    for name in ["test.log", "compile.log", "elaborate.log", "board_reset_tb.sv"]:
        shutil.copy2(test / name, target / name)
    (target / "verification.json").write_text(json.dumps(data, indent=2) + "\n")
    print("PASS board boundary: TX/RX 64 bytes, DDR250 edges, 2 x 10ms reset, stopped RX/calibration recovery")
if __name__ == "__main__": main()
