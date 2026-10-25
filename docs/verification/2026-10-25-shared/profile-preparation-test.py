from pathlib import Path
import subprocess,json,hashlib
root=Path('/home/openion/Valence-Net')
before=json.loads((root/'build/verified.json').read_text())
records={}
for profile in ['bram','auto','hybrid']:
    with (root/f'build/prepare-profile-{profile}.log').open('w') as log:
        subprocess.run(['mill','-i','netstack.runMain','netstack.Emit','build',profile],cwd=root,stdout=log,stderr=subprocess.STDOUT,check=True)
        result=subprocess.run(['python3','fpga/prepare_registered_ooc.py','--memory-profile',profile],cwd=root,capture_output=True,text=True)
        log.write(result.stdout+result.stderr)
        result.check_returncode()
    run=Path(result.stdout.splitlines()[0])
    record=json.loads((run/'provenance.json').read_text())
    assert record['reemission_memory_profile']==profile
    assert record['fresh_emission_check']['matched']
    assert record['memory_mapping']['MultiEthernetTcpUdpEndpoint']['profile']==profile
    assert record['source_verification']['source_sha256']==before['source_sha256']
    for name,sha in record['rtl_sha256'].items():
        assert hashlib.sha256((run/name).read_bytes()).hexdigest()==sha
    attributes=record['memory_mapping']['MultiEthernetTcpUdpEndpoint']['attribute_files']
    assert profile!='auto' or not attributes
    assert profile!='bram' or all(x['ultra']==0 for x in attributes.values())
    if profile=='hybrid':
        original=json.loads((root/'build/vivado/20261025T082252Z/provenance.json').read_text())
        assert record['rtl_sha256']==original['rtl_sha256']
    records[profile]={'run':str(run),'provenance_sha256':hashlib.sha256((run/'provenance.json').read_bytes()).hexdigest(),
        'attribute_files':attributes,'fresh_emission_matched':True,'routed_timing_verified':False}
for name,sha in before['source_sha256'].items():
    assert hashlib.sha256((root/name).read_bytes()).hexdigest()==sha
assert json.loads((root/'build/verified.json').read_text())==before
(root/'build/profile-preparation-results.json').write_text(json.dumps({
    'source_sha256':before['source_sha256'],'profiles':records,
    'default_rtl_identical_to_active_CAD':True,'functional_manifest_unchanged':True},indent=2)+'\n')
print('PROFILE_PREPARATION_PASS bram/auto/hybrid; default RTL byte-identical to active CAD',flush=True)
