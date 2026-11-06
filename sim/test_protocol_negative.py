#!/usr/bin/env python3
"""Independent iperf3 control-wire oracle for bounds, JSON and cookie rejection."""
import json,pathlib,socket,struct,subprocess,time
ROOT=pathlib.Path(__file__).resolve().parents[1];OUT=ROOT/"build/app"
COOKIE=b"01234567-89ab-cdef-0123-456789abcdef\0"
assert len(COOKIE)==37
def exact(s,n):
    b=b""
    while len(b)<n:
        more=s.recv(n-len(b))
        if not more:raise AssertionError("early EOF")
        b+=more
    return b
def connect():
    s=socket.create_connection(("127.0.0.1",15202),3);s.settimeout(3)
    # This peer deliberately fragments the control cookie into individual bytes.
    for c in COOKIE:s.sendall(bytes([c]))
    assert exact(s,1)==b"\x09"
    return s
def error(s):
    assert exact(s,9)==b"\xfe"+struct.pack("!II",13,0)
    s.close();time.sleep(.02)
def main():
    results=[]
    with (OUT/"negative-server.log").open("w") as log:
        server=subprocess.Popen([OUT/"test","15202"],stdout=log,stderr=subprocess.STDOUT)
        try:
            for _ in range(100):
                if "READY" in (OUT/"negative-server.log").read_text():break
                time.sleep(.01)
            for name,payload in [
                ("missing_tcp",b'{"parallel":1}'),
                ("duplicate_key",b'{"tcp":true,"reverse":false,"reverse":true}'),
                ("invalid_numeric",b'{"tcp":true,"parallel":01}'),
                ("nested_value",b'{"tcp":true,"parallel":[1]}'),
                ("truncated_json",b'{"tcp":true'),
                ("oversize_key",b'{"tcp":true,"this_key_is_far_too_long_to_be_a_parameter":0}'),
                ("omit_rejected",b'{"tcp":true,"omit":1}'),
                ("bidir_rejected",b'{"tcp":true,"bidirectional":true}'),
                ("authentication_rejected",b'{"tcp":true,"authtoken":"x"}')]:
                s=connect();s.sendall(struct.pack("!I",len(payload))+payload);error(s)
                results.append(name);print("PASS",name,flush=True)
            s=connect();s.sendall(struct.pack("!I",4097));error(s)
            results.append("length_bound");print("PASS length_bound",flush=True)
            s=connect()
            payload=json.dumps({"tcp":True,"parallel":1,"title":'metadata "reverse": true'}).encode()
            s.sendall(struct.pack("!I",len(payload))+payload)
            assert exact(s,1)==b"\x0a"
            data=socket.create_connection(("127.0.0.1",15202),3)
            data.sendall(b"x"*37)
            error(s);data.close()
            results.append("cookie_mismatch");print("PASS cookie_mismatch",flush=True)
        finally:
            server.terminate();server.wait(timeout=5)
    (OUT/"negative.json").write_text(json.dumps({"passed":results},indent=2)+"\n")
if __name__=="__main__":main()
