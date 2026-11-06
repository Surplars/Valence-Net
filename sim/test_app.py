#!/usr/bin/env python3
"""Real iperf3 against the GSIM hardware application over a software TCP shim.
This stage tests iperf3 framing/fragmentation/backpressure, not the TCP/MAC core.
"""
import json, pathlib, subprocess, time
ROOT=pathlib.Path(__file__).resolve().parents[1]
OUT=ROOT/"build/app"; CLIENT=ROOT/"tools/iperf3-src/src/iperf3"
def main():
    results=[]
    with (OUT/"server.log").open("w") as log:
        server=subprocess.Popen([OUT/"test","15201"],stdout=log,stderr=subprocess.STDOUT)
        try:
            for _ in range(200):
                if "READY" in (OUT/"server.log").read_text(): break
                if server.poll() is not None: raise RuntimeError("model exited")
                time.sleep(.01)
            for name,args in [
                ("forward_bytes",["-n","65536","-l","1024"]),
                ("reverse_bytes",["-n","65536","-l","1024","-R"]),
                ("forward_time",["-t","1","-l","1024"]),
                ("reverse_time",["-t","1","-l","1024","-R"]),
                ("reject_parallel",["-P","2","-n","4096"]),
                ("reject_udp",["-u","-n","4096"]),
                ("after_reject",["-n","4096","-l","1024"])]:
                time.sleep(.05)
                cmd=[str(CLIENT),"-c","127.0.0.1","-p","15201","-J",*args]
                p=subprocess.run(cmd,capture_output=True,text=True,timeout=15)
                (OUT/(name+".json")).write_text(p.stdout)
                j=json.loads(p.stdout)
                if name.startswith("reject"):
                    assert p.returncode!=0 and "error" in j,(name,j)
                else:
                    assert p.returncode==0 and "error" not in j,(name,j)
                    assert j["end"]["sum_received"]["bytes"]>0,(name,j)
                    if name.endswith("bytes"): assert j["end"]["sum_received"]["bytes"]==65536,(name,j)
                results.append({"test":name,"passed":True,"command":cmd})
                print("PASS",name,flush=True)
        finally:
            server.terminate()
            try: server.wait(timeout=5)
            except subprocess.TimeoutExpired: server.kill();server.wait()
    (OUT/"interop.json").write_text(json.dumps(results,indent=2)+"\n")
if __name__=="__main__": main()
