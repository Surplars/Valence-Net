from pathlib import Path
import hashlib,json,subprocess
root=Path('/home/openion/Valence-Iperf-Rx256K');out=root/'build/rx-mixed-probe-settled';out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
model=root/'build/gmii';obj=root/'build/transport-comparison/model-64.o'
assert (root/'build/transport-comparison/model-64.sha256').read_text()==sha(model/'IperfGmiiHarness0.cpp')+sha(model/'IperfGmiiHarness.h')
prefix=(root/'sim/gmii_burst.cpp').read_text().split('int main(int argc,char**argv)')[0]
main=r'''
int main(int argc,char**argv){try{
 std::string mode=argc>1?argv[1]:"full";Peer p;p.start(false);p.idle(5000);p.until([&]{return p.pending.empty()&&p.gap==0;});
 uint64_t begin=p.cycle;unsigned total=262144,remaining=total,blockRemain=0,count=0;
 unsigned block=mode=="32K"?32768:mode=="64K"?65536:mode=="60K"?61440:0;
 while(remaining){unsigned n=1460;if(block){if(!blockRemain)blockRemain=block;n=std::min(n,blockRemain);blockRemain-=n;}
  if(mode=="short64"&&(count&1))n=64;
  if(mode=="short1"&&(count&1))n=1;
  if(mode=="captured"&&count%179==178)n=800;
  n=std::min(n,remaining);p.transmit(1,24,Bytes(n,0x5a));remaining-=n;count++;
  if(mode=="control-acks"&&count%8==0)p.ack(0,p.dutNext[0]);
 }
 p.until([&]{return p.pending.empty();},2000000);uint64_t wireCycles=p.cycle-begin;p.idle(20000);
 std::cout<<"RX_MIX mode="<<mode<<" frames="<<count<<" received="<<p.d.get_io$$rxBytes()<<" wire_cycles="<<wireCycles<<" busy_drops="<<p.busyDrops<<" other_drops="<<p.otherDrops<<" mac_drops="<<p.macDrops<<" ack_records="<<p.packets.size()<<std::endl;
 check(p.d.get_io$$rxBytes()==total,"mixed receive bytes lost");check(p.busyDrops==0&&p.macDrops==0,"mixed receive admission drops");return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}}
'''
src=out/'probe.cpp';src.write_text(prefix+main)
command=['clang++-19','-std=c++20','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(model),str(obj),str(src),'-ldl','-o',str(out/'probe')]
subprocess.run(command,check=True)
rows=[]
for mode in ['full','32K','64K','60K','short64','short1','captured','control-acks']:
 r=subprocess.run([str(out/'probe'),mode],capture_output=True,text=True,timeout=180);log=out/(mode+'.log');log.write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,flush=True);rows.append({'mode':mode,'passed':r.returncode==0,'log_sha256':sha(log)})
(out/'verification.json').write_text(json.dumps({'passed':all(r['passed'] for r in rows),'scope':'New mixed-length and concurrent control ACK diagnostic after board observations; external GMII only, same sealed GSIM network model','driver_sha256':sha(src),'model_source_sha256':sha(model/'IperfGmiiHarness0.cpp'),'cases':rows},indent=2)+'\n')
print('MIXED_RESULT',out,flush=True)