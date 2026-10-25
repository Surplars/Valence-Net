from pathlib import Path
import hashlib,json,shutil,subprocess,tempfile
root=Path('/home/openion/Valence-Net')
baseline=root/'build/vivado/20261024T074147Z'
with tempfile.TemporaryDirectory(prefix='console-auditor-',dir=root/'build') as directory:
    work=Path(directory)
    for p in baseline.iterdir():
        if p.name=='input':
            (work/'input').symlink_to(p,target_is_directory=True)
        elif p.is_file() and p.name in {'provenance.json','ooc.tcl','progress.txt','results.tsv','vivado.log','console.log'}:
            shutil.copy2(p,work/p.name)
    top='ValenceNetTimingTop'
    (work/top).mkdir()
    for p in (baseline/top).glob('*.rpt'):
        shutil.copy2(p,work/top/p.name)
    command=['python3',str(root/'fpga/summarize_ooc.py'),str(work)]
    subprocess.run(command,check=True,stdout=subprocess.DEVNULL)
    assert json.loads((work/'summary.json').read_text())['modules'][top]['routed_timing_verified']
    cases={}
    for encoding in ['utf-8','utf-8-sig','utf-16','utf-16-be']:
        text='ERROR: synthetic console decoding regression\n'
        data=text.encode(encoding)
        if encoding=='utf-16-be':data=bytes([254,255])+data
        (work/'console.log').write_bytes(data)
        subprocess.run(command,check=True,stdout=subprocess.DEVNULL)
        report=json.loads((work/'summary.json').read_text())
        assert report['native_console_errors']==['synthetic console decoding regression']
        assert not report['modules'][top]['routed_timing_verified']
        assert report['modules'][top]['numeric_timing_met']
        cases[encoding]='rejected qualification despite clean numeric timing'
    output={'baseline_clean_console_accepted':True,'injected_errors':cases,
            'auditor_sha256':hashlib.sha256((root/'fpga/summarize_ooc.py').read_bytes()).hexdigest(),
            'test_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'scope':'Auditor fixtures using a historical completed route; not new hardware timing evidence'}
    (root/'build/console-auditor-results.json').write_text(json.dumps(output,indent=2)+'\n')
print('CONSOLE_AUDITOR_PASS utf-8/utf-8-sig/utf-16le/utf-16be error detection',flush=True)
