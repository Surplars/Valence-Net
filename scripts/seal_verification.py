#!/usr/bin/env python3
"""Seal retained functional evidence without claiming physical/board throughput."""
import datetime,hashlib,json,pathlib,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    app=json.loads((ROOT/"build/app/interop.json").read_text())
    negative=json.loads((ROOT/"build/app/negative.json").read_text())
    wire=json.loads((ROOT/"build/gmii/interop.json").read_text())
    assert len(app)==7 and all(t["passed"] for t in app)
    assert len(negative["passed"])==11
    assert len(wire["tests"])==3 and all(t["passed"] for t in wire["tests"])
    windows={}
    for name in ["forward","reverse"]:
        p=ROOT/"build/windows-client"/(name+".json")
        j=json.loads(p.read_text(encoding="utf-8-sig"))
        assert "error" not in j and j["end"]["sum_received"]["bytes"]==65536
        windows[name]={"version":j["start"]["version"],"bytes":65536,"sha256":sha(p)}
    source=list((ROOT/"src").rglob("*.scala"))+[ROOT/"build.mill",ROOT/"dependencies.json"]
    evidence=[]
    for name in ["app","gmii"]:
        base=ROOT/"build"/name
        evidence += list(base.glob("*.fir"))+list(base.glob("*.h"))+list(base.glob("*.cpp"))
        evidence += list(base.glob("*.json"))+list(base.glob("*test.log"))+[base/"test"]
    equivalence=ROOT/"build/gmii-equivalence/gmii.fir"
    current_equivalent=None
    if equivalence.exists():
        assert sha(equivalence)==sha(ROOT/"build/gmii/gmii.fir")
        current_equivalent={"sha256":sha(equivalence),"identical_to_tested_model":True}
    app_equivalence_path=ROOT/"build/app-equivalence/verification.json"
    app_equivalence=None
    if app_equivalence_path.exists():
        app_equivalence=json.loads(app_equivalence_path.read_text())
        assert app_equivalence["gsim_model_byte_identical"] and app_equivalence["rtl_byte_identical"]
        assert sha(ROOT/"build/app/app.fir") in (app_equivalence["prior_fir_sha256"],app_equivalence["current_fir_sha256"])
        assert sha(ROOT/"build/app-equivalence/app.fir")==app_equivalence["current_fir_sha256"]
        for name,digest in app_equivalence["gsim_model_sha256"].items():
            assert sha(ROOT/"build/app"/name)==digest
            assert sha(ROOT/"build/app-equivalence"/name)==digest
        for name,digest in app_equivalence["rtl_sha256"].items():
            assert sha(ROOT/"build/app/rtl"/name)==digest
            assert sha(ROOT/"build/app-equivalence/rtl"/name)==digest
        evidence += [app_equivalence_path,ROOT/"build/app-equivalence/app.fir"]
    diagnostic_equivalence_path=ROOT/"build/gmii-diagnostics-equivalence/verification.json"
    diagnostic_equivalence=None
    if diagnostic_equivalence_path.exists():
        diagnostic_equivalence=json.loads(diagnostic_equivalence_path.read_text())
        assert diagnostic_equivalence["rtl_byte_identical"]
        assert sha(ROOT/"build/gmii/gmii.fir")==diagnostic_equivalence["baseline_fir_sha256"]
        assert sha(ROOT/"build/gmii-diagnostics-equivalence/gmii.fir")==diagnostic_equivalence["current_fir_sha256"]
        for name,digest in diagnostic_equivalence["rtl_sha256"].items():
            assert sha(ROOT/"build/gmii/rtl"/name)==digest
            assert sha(ROOT/"build/gmii-diagnostics-equivalence/rtl"/name)==digest
        evidence += [diagnostic_equivalence_path,ROOT/"build/gmii-diagnostics-equivalence/gmii.fir"]
    current_model_path=ROOT/"build/gmii/model-provenance.json"
    current_model=json.loads(current_model_path.read_text())
    assert current_model["target"]=="gmii" and current_model["gsim_revision"]=="93b8cd23edd3228807c4f2a08c19c3936a463cb2"
    for rel,digest in current_model["source_sha256"].items():
        assert sha(ROOT/rel)==digest,("Current GMII model source changed",rel)
    for rel,digest in current_model["model_sha256"].items():
        assert sha(ROOT/"build/gmii"/rel)==digest,("Current GMII model changed",rel)
    evidence.append(current_model_path)
    extra_models={}
    for kind in ["app","rxqueue"]:
        model_path=ROOT/"build"/kind/"model-provenance.json"
        record=json.loads(model_path.read_text())
        assert record["target"]==kind
        for rel,digest in record["source_sha256"].items():assert sha(ROOT/rel)==digest,(kind,rel)
        for rel,digest in record["model_sha256"].items():assert sha(ROOT/"build"/kind/rel)==digest,(kind,rel)
        extra_models[kind]=record;evidence.append(model_path)
    receive_window=json.loads((ROOT/"build/rxqueue/verification.json").read_text())
    assert receive_window["passed"] and receive_window["capacity_bytes"]==262144
    assert sha(ROOT/"sim/receive_window.cpp")==receive_window["test_driver_sha256"]
    assert sha(ROOT/"build/rxqueue/test.log")==receive_window["test_log_sha256"]
    assert sha(ROOT/"build/rxqueue/model-provenance.json")==receive_window["model_provenance_sha256"]
    data={
        "sealed_utc":datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "dependencies":json.loads((ROOT/"dependencies.json").read_text()),
        "source_sha256":{str(p.relative_to(ROOT)):sha(p) for p in sorted(source)},
        "evidence_sha256":{str(p.relative_to(ROOT)):sha(p) for p in sorted(set(evidence)) if p.is_file()},
        "current_gmii_model_provenance":current_model,
        "additional_model_provenance":extra_models,"receive_window_validation":receive_window,
        "gmii_board_only_change_equivalence":current_equivalent,
        "retained_app_model_equivalence":app_equivalence,
        "retained_gmii_diagnostic_rtl_equivalence":diagnostic_equivalence,
        "application_interop":app,"negative_cases":negative,"gmii_interop":wire,
        "user_windows_iperf3":windows,
        "simulation_throughput_is_board_throughput":False,
        "board_programmed":False,"board_iperf_measured":False
    }
    (ROOT/"build/functional-verification.json").write_text(json.dumps(data,indent=2)+"\n")
    print("PASS: 7 application cases, 11 negative cases, 3 full GMII cases, 2 original Windows iperf3 cases")
if __name__=="__main__":main()
