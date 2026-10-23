#include "TcpCongestedEndpointHarness.h"
#include "tcp_endpoint_peer.h"
using Test=TcpEndpointPeer<STcpCongestedEndpointHarness>;
int main(){try{
 Test t; unsigned cases=0, cacheDrops=0;
 auto fresh=[&](uint32_t peer){
  if(t.d.get_io$$state()!=0){t.close(true);t.drain();}
  t.active(uint32_t(t.rng()),peer);
 };
 auto pattern=[&](unsigned n){std::vector<uint8_t> v(n);for(auto&x:v)x=t.rng();return v;};
 auto expect=[&](const std::vector<uint8_t>&v,unsigned a,unsigned b){
  for(unsigned i=a;i<b;++i)t.expectedRx.push_back(v.at(i));
 };
 auto send=[&](uint32_t base,const std::vector<uint8_t>&v,unsigned a,unsigned b,unsigned flags=24){
  t.wire(t.encode(base+a,t.planned,flags,65535,{v.begin()+a,v.begin()+b}));
  for(unsigned i=0;i<180;++i){t.step();cacheDrops+=t.d.get_io$$reorderDropped();}
 };
 auto finish=[&](uint32_t next){
  t.peerNext=next;t.until([&]{return t.d.get_io$$receiveNext()==next&&t.expectedRx.empty()&&!t.d.get_io$$reorderCount();});
  t.idle(100);t.drain();
  check(u32(t.packets.back().bytes,28)==next,"final cumulative ACK omitted reassembled bytes");
  check(t.d.get_io$$receiveWindow()==1024,"reassembly leaked receive credit");++cases;
 };
 // Reverse and shuffled arrivals, including sequence wrap and byte-sized holes.
 for(unsigned k=0;k<24;++k){
  fresh(k==0?0xfffffff0:uint32_t(t.rng()));uint32_t b=t.peerNext;auto v=pattern(448);
  unsigned before=t.receivedBytes;expect(v,0,v.size());
  std::vector<unsigned> order={1,2,3,4,5,6};std::shuffle(order.begin(),order.end(),t.rng);
  for(unsigned i:order){send(b,v,i*64,(i+1)*64);
   check(t.d.get_io$$receiveNext()==b&&t.receivedBytes==before,"gap exposed bytes or advanced cumulative ACK");}
  check(t.d.get_io$$reorderCount()==6,"shuffled segments not retained");
  send(b,v,0,64);finish(b+v.size());
 }
 // Duplicate/partial overlap admission is bounded: retain the first range.
 // The later in-order prefix overlaps it, requiring trimming on replay.
 fresh(30000);uint32_t b=t.peerNext;auto v=pattern(750);expect(v,0,700);
 send(b,v,100,700);send(b,v,100,700);send(b,v,650,750);
 check(t.d.get_io$$reorderCount()==1,"duplicate/overlap consumed another bank");
 send(b,v,0,400);finish(b+700);
 expect(v,700,750);send(b,v,700,750);finish(b+750);
 // A new contiguous segment can cover a cached range entirely.
 fresh(40000);b=t.peerNext;v=pattern(600);expect(v,0,600);
 send(b,v,150,400);send(b,v,0,600);finish(b+600);
 // Eight slots fill; the ninth gap is dropped, but the missing prefix must
 // still enter. The sender then recovers the dropped segment by retransmit.
 fresh(50000);b=t.peerNext;v=pattern(320);expect(v,0,288);
 for(unsigned i=1;i<=9;++i)send(b,v,i*32,(i+1)*32);
 check(t.d.get_io$$reorderCount()==8,"bounded cache admission");
 send(b,v,0,32);finish(b+288);
 expect(v,288,320);send(b,v,288,320);finish(b+320);
 // Window-edge clipping retains only the already advertised range. Slow
 // consumers must see exactly those bytes and an honest zero window.
 fresh(60000);b=t.peerNext;v=pattern(1100);expect(v,0,1024);t.stallReceive=true;
 send(b,v,900,1100);send(b,v,0,900);t.idle(500);
 check(t.d.get_io$$receiveNext()==b+1024&&t.d.get_io$$receiveWindow()==0,"window edge clipping");
 check(t.expectedRx.size()==1024,"stalled receive data was consumed");
 send(b,v,1024,1100);check(t.d.get_io$$receiveNext()==b+1024,"zero-window sequence advanced");
 t.stallReceive=false;finish(b+1024);
 expect(v,1024,1100);send(b,v,1024,1100);finish(b+1100);
 // A cached FIN is sequence ordered; later cached data must be discarded.
 for(bool finOnly:{false,true}){
  fresh(70000+unsigned(finOnly)*1000);b=t.peerNext;v=pattern(400);
  unsigned end=finOnly?100:200;expect(v,0,end);
  send(b,v,300,400);send(b,v,100,end,17);
  check(t.d.get_io$$state()==4&&t.d.get_io$$receiveNext()==b,"future FIN closed early");
  send(b,v,0,100);finish(b+end+1);
  check(t.d.get_io$$state()==7,"ordered FIN did not enter CLOSE-WAIT");
 }
 // Invalid ACK, wrong port, corrupt checksum and late stream errors must
 // never be promoted later just because a missing prefix arrives.
 fresh(80000);b=t.peerNext;v=pattern(200);
 auto bad=t.encode(b+100,t.planned+1234,24,65535,{v.begin()+100,v.end()});
 t.wire(bad);t.idle(200);
 bad=t.encode(b+100,t.planned,24,65535,{v.begin()+100,v.end()});bad.back()^=1;
 t.wire(bad);t.idle(200);
 bad=t.encode(b+100,t.planned,24,65535,{v.begin()+100,v.end()});t.wire(bad,true);t.idle(200);
 bad=t.encode(b+100,t.planned,24,65535,{v.begin()+100,v.end()});
 Test::put16(bad,22,12346);Test::put16(bad,36,0);
 std::vector<uint8_t> pseudo(12);Test::put32(pseudo,0,0x0a000002);Test::put32(pseudo,4,0x0a000001);
 pseudo[9]=6;Test::put16(pseudo,10,bad.size()-20);pseudo.insert(pseudo.end(),bad.begin()+20,bad.end());
 Test::put16(bad,36,sum(pseudo));t.wire(bad);t.idle(200);
 check(t.d.get_io$$reorderCount()==0,"invalid segment entered reorder storage");
 expect(v,0,100);send(b,v,0,100);finish(b+100);
 expect(v,100,200);send(b,v,100,200);finish(b+200);
 // Aborts at multiple phases of admission/copy/replay must release every
 // cache token and must not publish a previous generation's data after OPEN.
 for(bool promote:{false,true})for(unsigned delay:{0U,4U,12U,36U,100U}){
  fresh(uint32_t(t.rng()));b=t.peerNext;v=pattern(900);t.stallReceive=true;
  t.wire(t.encode(b+64,t.planned,24,65535,{v.begin()+64,v.end()}));
  if(promote){t.idle(200);t.wire(t.encode(b,t.planned,24,65535,{v.begin(),v.begin()+64}));}
  t.idle(delay);t.close(true);t.drain();t.idle(200);
  check(t.d.get_io$$reorderCount()==0,"abort leaked cached entries");
  t.stallReceive=false;fresh(uint32_t(t.rng()));b=t.peerNext;v=pattern(32);
  expect(v,0,32);send(b,v,0,32);finish(b+32);
 }
 // Replaying retained data must not re-apply its old peer ACK/window. Keep
 // TX data outstanding while the peer advances its ACK on the hole filler.
 fresh(90000);b=t.peerNext;v=pattern(400);
 t.write(512);t.until([&]{return t.d.get_io$$sendActual()==t.planned;});t.drain();
 uint32_t old=t.planned-512;
 expect(v,0,400);
 for(unsigned off=100;off<400;off+=100){
  t.wire(t.encode(b+off,old,24,65535,{v.begin()+off,v.begin()+off+100}));t.idle(200);
 }
 unsigned retries=t.replayed;t.peerNext=b+400;
 send(b,v,0,100);finish(b+400);
 check(t.d.get_io$$sendOldest()==t.planned&&!t.d.get_io$$fastRecovery()&&t.replayed==retries,
       "reassembly reprocessed old ACKs");
 check(cacheDrops>=3,"cache exhaustion/overlap drop paths untested");
 t.close(true);t.drain();
 std::cout<<"TCP_REASSEMBLY_PASS cases="<<cases<<" cycles="<<t.cycles<<" bytes="<<t.receivedBytes
  <<" observed_cache_drops="<<cacheDrops
  <<" wrap=pass overlap=pass exhaustion=pass window_clip=pass ordered_fin=pass"
    " invalid_admission=pass abort_reuse=pass old_ack_isolation=pass randomized_backpressure=pass\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
