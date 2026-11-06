"""Freeze a small placement ECO on the physically optimized TX64 netlist."""
from pathlib import Path
import datetime
import hashlib
import json
import shutil

root = Path(__file__).resolve().parents[1]
base = Path('/mnt/e/VM/Share/Valence-rtl')
old = Path((root / 'build/latest-vivado.txt').read_text().strip()).resolve()
assert base.resolve() in old.parents
if (not (old / 'routed.dcp').is_file() or
        json.loads((old / 'provenance.json').read_text()).get('implementation_retry', {}).get('native_tx_placement')):
    old = Path(json.loads((old / 'provenance.json').read_text())['implementation_retry']['directory']).resolve()
    assert base.resolve() in old.parents and (old / 'routed.dcp').is_file()
gold = base / 'valence-iperf-20261024T051245Z'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
previous = json.loads((old / 'provenance.json').read_text())
for name, digest in previous['input_sha256'].items():
    assert sha(old / name) == digest, name
for name, digest in previous['source_sha256'].items():
    if name.startswith('src/') or name in ['build.mill', 'fpga/iperf_board.sv', 'fpga/iperf_uart_log.sv', 'fpga/board.xdc', 'fpga/cdc.tcl', 'fpga/native_gmac_clocks.sv']:
        assert sha(root / name) == digest, name
qualified = json.loads((gold / 'physical-verification.json').read_text())
assert sha(gold / 'routed.dcp') == qualified['output_sha256']['routed.dcp']
gold_manifest = json.loads((gold / 'provenance.json').read_text())
for name in ['native_rgmii.sv', 'native_gmac_clocks.sv', 'native_gmac_divided_clock.sv', 'native_gmac_pins.xdc', 'native_quarter_clock_constraints.tcl', 'board.xdc']:
    assert sha(old / name) == gold_manifest['input_sha256'][name], name
assert not (old / 'valence_iperf.bit').exists()
stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
run = base / ('valence-iperf-' + stamp)
run.mkdir()
for name in previous['input_sha256']:
    target = run / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(old / name, target)
shutil.copy2(old / 'build.tcl', run / 'parent_build.tcl')
shutil.copy2(root / 'fpga/build_tx_placement.tcl', run / 'build.tcl')
shutil.copy2(root / 'fpga/native_tx_placement.tcl', run / 'native_tx_placement.tcl')
shutil.copy2(root / 'fpga/gold-native-layout.tsv', run / 'gold-native-layout.tsv')
shutil.copy2(gold / 'physical-verification.json', run / 'gold-placement-source-verification.json')
shutil.copy2(old / 'routed.dcp', run / 'imported_routed.dcp')
metadata = dict(previous)
metadata.update(created_utc=stamp, resumed_synthesis=None, resumed_routing=None)
metadata['implementation_retry'] = {
    'directory': str(old), 'checkpoint_sha256': sha(run / 'imported_routed.dcp'),
    'parent_manifest_sha256': sha(old / 'provenance.json'),
    'build_script_source': 'fpga/build_tx_placement.tcl',
    'native_tx_placement': True,
    'gold_directory': str(gold), 'gold_checkpoint_sha256': qualified['output_sha256']['routed.dcp'],
    'scope': 'Same RTL, netlist and timing constraints; Preserve clock trunks and existing routing; relocate nine TX pad FFs, nine input FFs, one reset-release FF and five symbol/phase LUTs near fixed serializers; individually place displaced FFs and reroute affected sinks',
}
sources = sorted((root / 'src').rglob('*.scala')) + sorted((root / 'fpga').glob('*')) + [root / 'build.mill']
metadata['source_sha256'] = {str(p.relative_to(root)): sha(p) for p in sources if p.is_file()}
metadata['input_sha256'] = {str(p.relative_to(run)): sha(p) for p in run.rglob('*') if p.is_file()}
(run / 'provenance.json').write_text(json.dumps(metadata, indent=2) + '\n')
(root / 'build/latest-vivado.txt').write_text(str(run) + '\n')
progress = root / 'build/tx64-progress.json'
d = json.loads(progress.read_text())
d.update(run=str(run), status='native_tx_placement_retry', parent_physical_retry=str(old), board_programmed=False)
progress.write_text(json.dumps(d, indent=2) + '\n')
print(run)
