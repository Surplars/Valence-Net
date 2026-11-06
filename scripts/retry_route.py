"""Freeze a physical-only timing retry without weakening any constraint."""
import datetime
import hashlib
import json
from pathlib import Path
import shutil

root = Path(__file__).resolve().parents[1]
base = Path('/mnt/e/VM/Share/Valence-rtl')
old = base / 'valence-iperf-20261024T064234Z'
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
previous = json.loads((old / 'provenance.json').read_text())
for relative, digest in previous['input_sha256'].items():
    assert sha(old / relative) == digest, relative
for relative, digest in previous['source_sha256'].items():
    if relative.startswith('src/') or relative in ['build.mill', 'fpga/iperf_board.sv', 'fpga/iperf_uart_log.sv', 'fpga/board.xdc', 'fpga/cdc.tcl', 'fpga/native_gmac_clocks.sv']:
        assert sha(root / relative) == digest, relative
assert (old / 'routed.dcp').is_file()
assert not (old / 'valence_iperf.bit').exists()
stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
run = base / ('valence-iperf-' + stamp)
run.mkdir()
for relative in previous['input_sha256']:
    destination = run / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(old / relative, destination)
shutil.copy2(run / 'build.tcl', run / 'initial_build.tcl')
shutil.copy2(root / 'fpga/retry_timing.tcl', run / 'build.tcl')
shutil.copy2(old / 'board.fir', run / 'board.fir')
shutil.copy2(old / 'routed.dcp', run / 'imported_routed.dcp')
metadata = dict(previous)
metadata['created_utc'] = stamp
metadata['resumed_synthesis'] = None
metadata['resumed_routing'] = None
metadata['implementation_retry'] = {
    'directory': str(old),
    'checkpoint_sha256': sha(run / 'imported_routed.dcp'),
    'initial_manifest_sha256': sha(old / 'provenance.json'),
    'build_script_source': 'fpga/retry_timing.tcl',
    'scope': 'Exact same routed logic, IO, clocks and constraints; AggressiveExplore physical optimization and rerouting only',
    'initial_setup_slack_ns': -0.369,
    'initial_hold_slack_ns': 0.010,
}
sources = sorted((root / 'src').rglob('*.scala')) + sorted((root / 'fpga').glob('*')) + [root / 'build.mill']
metadata['source_sha256'] = {str(p.relative_to(root)): sha(p) for p in sources if p.is_file()}
metadata['input_sha256'] = {str(p.relative_to(run)): sha(p) for p in run.rglob('*') if p.is_file()}
(run / 'provenance.json').write_text(json.dumps(metadata, indent=2) + '\n')
(root / 'build/latest-vivado.txt').write_text(str(run) + '\n')
progress = root / 'build/tx64-progress.json'
data = json.loads(progress.read_text())
data.update(run=str(run), status='physical_only_timing_retry', failed_initial_run=str(old),
            initial_setup_slack_ns=-0.369, board_programmed=False, board_iperf_measured=False)
progress.write_text(json.dumps(data, indent=2) + '\n')
print(run)
