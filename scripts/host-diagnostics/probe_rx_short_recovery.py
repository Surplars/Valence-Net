from pathlib import Path
import hashlib,json,subprocess
root=Path('/home/openion/Valence-Iperf-Rx256K');out=root/'build/rx-short-recovery';out.mkdir(exist_ok=True)
source=(root/'build/rx-mixed-probe-settled/probe.cpp').read_text()
source=source.replace('p.idle(20000);','p.idle(20000); uint64_t beforeRecovery=p.d.get_io$$rxBytes();check(beforeRecovery==total-16,"expected isolated missing short tail");p.send(tcp(50001,p.hostSeq[1]-16,p.dutNext[1],24,Bytes(16,0x5a)));p.until([&]{return p.pending.empty();});p.idle(20000);std::cout<<"RETRANSMIT_RECOVERY before="<<beforeRecovery<<" after="<<p.d.get_io$$rxBytes()<<" retried_bytes=16"<<std::endl;')
source=source.replace('check(p.busyDrops==0&&p.macDrops==0,"mixed receive admission drops");','check(p.busyDrops==1&&p.macDrops==0,"expected exactly one recoverable busy drop");')
p=out/'probe.cpp';p.write_text(source)
cmd=['clang++-19','-std=c++20','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(root/'build/gmii'),str(root/'build/transport-comparison/model-64.o'),str(p),'-ldl','-o',str(out/'probe')]
subprocess.run(cmd,check=True);r=subprocess.run([str(out/'probe'),'short64'],capture_output=True,text=True,timeout=180);(out/'recovery.log').write_text(r.stdout+r.stderr)
print(r.stdout+r.stderr,flush=True);sha=lambda x:hashlib.sha256(x.read_bytes()).hexdigest()
(out/'verification.json').write_text(json.dumps({'passed':r.returncode==0,'scope':'External sender retransmission after one no-drop stress failure; does not erase the original minimum-frame admission limitation','original_busy_drop_count':1,'original_missing_bytes':16,'retransmitted_bytes':16,'final_expected_receive_bytes':262144,'driver_sha256':sha(p),'log_sha256':sha(out/'recovery.log')},indent=2)+'\n')
r.check_returncode()