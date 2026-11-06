#!/usr/bin/env python3
"""Isolated Linux TCP/IP <-> Ethernet/GMII <-> GSIM RTL interoperability."""
import json, os, pathlib, signal, subprocess, time
ROOT=pathlib.Path(__file__).resolve().parents[1]
OUT=ROOT/"build/gmii"; CLIENT=ROOT/"tools/iperf3-src/src/iperf3"
NS="valence-iperf-"+str(os.getpid())
def run(*args,**kw):
    return subprocess.run(list(map(str,args)),check=True,**kw)
def main():
    if os.geteuid()!=0: raise SystemExit("run under WSL root: creates a temporary isolated network namespace")
    server=None;created=False;results=[]
    try:
        run("ip","netns","add",NS);created=True
        run("ip","-n",NS,"link","set","lo","up")
        run("ip","netns","exec",NS,"ip","tuntap","add","dev","viperf0","mode","tap")
        run("ip","-n",NS,"link","set","viperf0","address","02:00:00:00:00:01")
        run("ip","-n",NS,"addr","add","192.168.137.1/24","dev","viperf0")
        run("ip","-n",NS,"link","set","viperf0","up")
        run("ip","netns","exec",NS,"ethtool","-K","viperf0","tso","off","gso","off","gro","off",
            stdout=subprocess.DEVNULL)
        with (OUT/"tap.log").open("w") as log:
            server=subprocess.Popen(["ip","netns","exec",NS,str(OUT/"test"),"viperf0"],
                stdout=log,stderr=subprocess.STDOUT)
            for _ in range(200):
                if "READY" in (OUT/"tap.log").read_text(): break
                if server.poll() is not None: raise RuntimeError("model exited")
                time.sleep(.01)
            p=run("ip","netns","exec",NS,"ping","-c","1","-W","5","192.168.137.10",capture_output=True,text=True)
            (OUT/"ping.log").write_text(p.stdout);print("PASS ARP/ICMP with bad FCS injection",flush=True)
            for name,args in [
                ("forward",["-n","32768","-l","1024"]),
                ("reverse_loss",["-n","32768","-l","1024","-R"]),
                ("repeat",["-n","4096","-l","1024"])]:
                time.sleep(.1)
                cmd=["ip","netns","exec",NS,str(CLIENT),"-c","192.168.137.10","-J","--connect-timeout","5000",*args]
                p=subprocess.run(cmd,capture_output=True,text=True,timeout=45)
                (OUT/(name+".json")).write_text(p.stdout)
                j=json.loads(p.stdout)
                assert p.returncode==0 and "error" not in j,(name,j)
                expected=4096 if name=="repeat" else 32768
                assert j["end"]["sum_received"]["bytes"]==expected,(name,j)
                results.append({"test":name,"bytes":expected,"passed":True})
                print("PASS",name,expected,flush=True)
            server.send_signal(signal.SIGTERM);server.wait(timeout=8)
            assert server.returncode==0,(OUT/"tap.log").read_text()[-4000:]
            logtext=(OUT/"tap.log").read_text()
            assert "network_loss_injected=1" in logtext
            assert "bad_fcs_drop_confirmed=1" in logtext
            (OUT/"interop.json").write_text(json.dumps({
                "scope":"full GMII MAC + hardware TCP/IP + iperf3 application",
                "host_ip":"192.168.137.1/24","dut_ip":"192.168.137.10/24",
                "tests":results,"bad_fcs_rejected":True,"loss_recovery":True,
                "board_throughput_measured":False},indent=2)+"\n")
    finally:
        if server is not None and server.poll() is None:
            server.terminate()
            try: server.wait(timeout=5)
            except subprocess.TimeoutExpired: server.kill();server.wait()
        if created: subprocess.run(["ip","netns","del",NS],check=True)
if __name__=="__main__": main()
