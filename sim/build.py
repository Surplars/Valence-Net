#!/usr/bin/env python3
import argparse, hashlib, json, os, pathlib, subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1]
GSIM=pathlib.Path(os.environ.get("GSIM_SOURCE",str(pathlib.Path.home()/".cache/valence-net/gsim-src-93b8cd23")))
REV="93b8cd23edd3228807c4f2a08c19c3936a463cb2"
def run(args,log=None):
    print("+",*map(str,args),flush=True)
    if log:
        with open(log,"w") as f: p=subprocess.run(list(map(str,args)),cwd=ROOT,stdout=f,stderr=subprocess.STDOUT)
        if p.returncode: print(pathlib.Path(log).read_text()[-12000:]); p.check_returncode()
    else: subprocess.run(list(map(str,args)),cwd=ROOT,check=True)
def seal_model(target,out):
    def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
    sources=sorted((ROOT/"src").rglob("*.scala"))+[ROOT/"build.mill",ROOT/"dependencies.json"]
    generated=[out/(target+".fir"),out/"test"]+sorted(out.glob("*.h"))+sorted(out.glob("*.cpp"))+sorted((out/"rtl").glob("*.sv"))
    record={"target":target,"gsim_revision":REV,"gsim_source":str(GSIM),"source_sha256":{str(p.relative_to(ROOT)):sha(p) for p in sources},"model_sha256":{str(p.relative_to(out)):sha(p) for p in generated},"hardware_throughput_claim":False}
    (out/"model-provenance.json").write_text(json.dumps(record,indent=2)+"\n")
def build(target,top,driver):
    out=ROOT/"build"/target; out.mkdir(parents=True,exist_ok=True)
    assert subprocess.check_output(["git","-C",str(GSIM),"rev-parse","HEAD"],text=True).strip()==REV
    run(["mill","-i","hw.runMain","iperf.Emit",target,out],out/"emit.log")
    assert (out/(target+".fir")).exists()
    run([GSIM/"build/gsim/gsim","--threads=1","--dir="+str(out),out/(target+".fir")],out/"generate.log")
    sources=sorted(out.glob(top+"[0-9]*.cpp"))
    assert sources
    run(["clang++-19","-std=c++20","-O1","-g","-fsanitize=address,undefined",
         "-fno-sanitize-recover=all","-I"+str(out),*sources,ROOT/"sim"/driver,"-ldl","-o",out/"test"],out/"compile.log")
    seal_model(target,out)
    return out/"test"
if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("target",choices=["app","gmii","rxqueue"])
    a=p.parse_args()
    build(a.target,{"app":"AppHarness","gmii":"IperfGmiiHarness","rxqueue":"ReceiveWindowHarness"}[a.target],
          {"app":"app_socket.cpp","gmii":"gmii_tap.cpp","rxqueue":"receive_window.cpp"}[a.target])
