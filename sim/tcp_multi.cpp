#include "MultiTcpEndpointHarness.h"
#include "multi_tcp_peer.h"
using Peer=MultiTcpPeer<SMultiTcpEndpointHarness>;
static void activeFour(Peer&p){
 for(unsigned c=0;c<4;++c)p.open(c,10000+c*1000,false,512);
 for(unsigned c=0;c<4;++c){p.select(c);p.until([&]{return p.d.get_io$$sendActual()==p.planned[c];});}
 for(unsigned c:{3U,1U,0U,2U}){p.peerNext[c]=50000+c*1000+1;p.rx(c,p.peerNext[c]-1,p.planned[c],18);}
 check(p.d.get_io$$states()==0x4444,"four simultaneous connections not established");p.drain();
}
static void independentData(){
 Peer p;activeFour(p);
 for(unsigned c=0;c<4;++c){
  auto v=p.pattern(93+c*7);p.expect(c,v);auto seq=p.peerNext[c];p.peerNext[c]+=v.size();
  std::vector<uint8_t> tail(v.begin()+31,v.end()),head(v.begin(),v.begin()+31);
  p.wire(p.encode(c,seq+31,p.planned[c],24,65535,tail));p.idle(100);
  p.select(c);check(p.d.get_io$$receiveNext()==seq&&p.d.get_io$$reorderCount()==1,"OOO state not per connection");
  p.wire(p.encode(c,seq,p.planned[c],24,65535,head));p.idle(300);
 }
 for(unsigned c=0;c<4;++c){p.select(c);check(p.d.get_io$$receiveNext()==p.peerNext[c]&&p.expectedRx[c].empty(),"receive/reassembly cross-connection leak");}
 for(unsigned c=0;c<4;++c)p.write(c,901+c*33);
 p.until([&]{return std::all_of(p.dataPackets.begin(),p.dataPackets.end(),[](unsigned n){return n>=1;});});p.drain();
 for(unsigned c=0;c<4;++c){p.select(c);check(p.d.get_io$$freeBanks()==3&&p.dataPackets[c]>=1,"independent retained TX state");}
 p.ack(1);p.ack(3);
 for(unsigned c=0;c<4;++c){p.select(c);check(p.d.get_io$$freeBanks()==(c%2?4:3),"ACK freed another connection's bank");}
 p.ack(0);p.ack(2);p.drain();
 auto retryBefore=p.replayed;
 p.write(2,129);p.drain();
 for(unsigned n=0;n<80&&p.replayed[2]==retryBefore[2];++n)p.idle(8,true);
 check(p.replayed[2]>retryBefore[2],"independent RTO did not retransmit");
 for(unsigned c:{0U,1U,3U})check(p.replayed[c]==retryBefore[c],"one timeout retransmitted another connection");
 p.ack(2);p.drain();
 p.rx(0,p.peerNext[0],p.planned[0],16,0);auto sent=p.dataPackets[0];p.write(0,37);
 p.write(1,87);p.idle(500);check(p.dataPackets[0]==sent&&p.dataPackets[1]>=3,"zero window blocked or leaked across connections");
 p.rx(0,p.peerNext[0],p.planned[0]-37,16,65535);p.until([&]{return p.dataPackets[0]>sent;});p.ack(0);p.ack(1);p.drain();
 p.stallReceiveMask=1;
 auto blocked=p.pattern(1300);p.receive(0,blocked);p.select(0);auto window=p.d.get_io$$receiveWindow();check(window<2048,"stalled receive did not retain credit");
 for(unsigned c=1;c<4;++c)p.receive(c,p.pattern(157+c));
 for(unsigned c=1;c<4;++c)check(p.expectedRx[c].empty(),"stalled connection blocked another receiver");
 p.stallReceiveMask=0;p.until([&]{return p.expectedRx[0].empty();});p.drain();
 auto drops=p.drops;auto bad=p.encode(1,p.peerNext[1],p.planned[1],24,65535,{1,2,3});bad.back()^=0x80;p.wire(bad);
 auto unknown=p.encode(1,p.peerNext[1],p.planned[1]);put16(unknown,22,9999);p.wire(unknown);p.idle(300);
 check(p.drops>=drops+2&&p.d.get_io$$states()==0x4444,"bad/unknown packet isolation");
 // Fill a blocked control path rather than merely retaining a few RX bytes.
 // Incoming packets for that context must be dropped without wedging the MAC.
 p.d.set_io$$noticeReadyMask(14);
 p.rx(0,p.peerNext[0],p.planned[0],17);++p.peerNext[0];
 auto busy=p.busyDrops;
 for(unsigned n=0;n<12;++n)p.wire(p.encode(0,p.peerNext[0],p.planned[0]));
 for(unsigned c=1;c<4;++c)p.receive(c,p.pattern(37+c));
 check(p.busyDrops>busy,"blocked context never exercised bounded ingress drop");
 for(unsigned c=1;c<4;++c)check(p.expectedRx[c].empty(),"busy ingress context blocked another tuple");
 p.d.set_io$$noticeReadyMask(15);p.idle(500);
 std::cout<<"multi independent: four active tuples, reordered RX, per-context windows/storage/RTO and bad packets passed\n";
}
static void listeners(){
 Peer p;p.random=false;
 for(unsigned c=0;c<4;++c)p.open(c,20000+c*1000,true,512);
 auto bad=p.encode(0,60000,0,2);bad.back()^=1;p.wire(bad);p.idle(160);
 check(p.d.get_io$$states()==0x1111&&p.drops==1,"bad SYN consumed shared listener");
 auto shortIp=p.encode(0,60000,0,2);shortIp.resize(20);p.wire(shortIp);p.idle(30);
 for(unsigned c=0;c<4;++c){
  p.peerNext[c]=60001+c*1000;p.wire(p.encode(c,p.peerNext[c]-1,0,2));
  if(c==0)p.wire(p.encode(c,p.peerNext[c]-1,0,2)); // retransmitted SYN must keep owner
 }
 p.idle(600);check(p.d.get_io$$states()==0x3333,"shared-port SYN dispatch or duplicate ownership failed");
 for(unsigned c:{2U,0U,3U,1U})p.rx(c,p.peerNext[c],p.planned[c]);
 check(p.d.get_io$$states()==0x4444,"passive handshakes not independent");
 for(unsigned c=0;c<4;++c)p.receive(c,p.pattern(41+c));
 p.drain();
 Peer q;q.random=false;q.open(0,70000,true,256);q.peerNext[0]=80001;
 q.rx(0,80000,0,2);q.drain();check(q.state(0)==3,"passive SYN failed");
 q.rx(0,80001,0,4);q.drain();check(q.state(0)==1,"RST did not restore LISTEN");
 q.remoteIp[0]=0x0a000008;q.remotePort[0]=9000;q.peerNext[0]=90001;
 q.rx(0,90000,0,2);q.drain();check(q.state(0)==3,"old peer claim stuck after RST to LISTEN");
 q.rx(0,90001,q.planned[0]);check(q.state(0)==4,"new peer cannot reuse listener");
 // Existing passive-owned four-tuple must also reject an active OPEN elsewhere.
 q.d.set_io$$passive(0);q.d.set_io$$localPort(q.localPort[0]);q.d.set_io$$remotePort(q.remotePort[0]);q.d.set_io$$remoteIp(q.remoteIp[0]);
 q.d.set_io$$openMask(2);q.idle(20);check(!(q.d.get_io$$openReady()&2)&&(q.d.get_io$$openConflict()&2)&&q.state(1)==0,"duplicate active tuple accepted");
 q.d.set_io$$openMask(0);
 Peer r;r.random=false;r.d.set_io$$initialSequence(123);r.d.set_io$$localMss(256);r.d.set_io$$openMask(15);
 unsigned openGuard=0;do{r.step();check(++openGuard<20,"simultaneous OPEN qualification stalled");}while(!r.d.get_io$$openReady());
 check(__builtin_popcount(r.d.get_io$$openReady())==1,"simultaneous identical opens accepted twice");r.d.set_io$$openMask(0);r.idle(10);
 check(r.d.get_io$$states()==2,"duplicate simultaneous active OPEN allocated several contexts");
 // An occupied low slot is not eligible and must not block identical requests
 // to otherwise free slots; slot 1 wins over slots 2 and 3 in parallel.
 Peer u;u.random=false;u.active(0,310000,320000,256);
 u.d.set_io$$remoteIp(u.remoteIp[1]);u.d.set_io$$remotePort(u.remotePort[1]);
 u.d.set_io$$localPort(u.localPort[1]);u.d.set_io$$initialSequence(330000);
 u.d.set_io$$localMss(256);u.d.set_io$$passive(0);u.d.set_io$$openMask(15);
 openGuard=0;do{u.step();check(++openGuard<20,"eligible OPEN qualification stalled");}while(!u.d.get_io$$openReady());
 check(u.d.get_io$$openReady()==2,"ineligible low OPEN blocked the lowest eligible tuple request");
 u.d.set_io$$openMask(0);u.idle(30);
 check(u.state(0)==4&&u.state(1)==2&&u.state(2)==0&&u.state(3)==0,
       "parallel OPEN eligibility changed unrelated state or accepted duplicate tuple");
 // A newly appearing low slot cannot reuse the claim granted to slot 3.
 Peer late;late.random=false;late.d.set_io$$initialSequence(9876);late.d.set_io$$localMss(256);
 late.remoteIp[3]=late.remoteIp[0];late.remotePort[3]=late.remotePort[0];
 late.d.set_io$$openMask(8);late.step();late.d.set_io$$openMask(9);late.step();
 late.d.set_io$$openMask(1);late.idle(20);
 check(late.state(3)==2&&late.state(0)==0&&(late.d.get_io$$openConflict()&1),
       "new lower OPEN bypassed a newly claimed tuple");late.d.set_io$$openMask(0);
 // A request changed after qualification must be checked as a fresh tuple.
 Peer changed;changed.random=false;changed.active(0,10000,20000,512);
 changed.d.set_io$$localPort(changed.localPort[0]+1);changed.d.set_io$$remoteIp(0x0a000009);
 changed.d.set_io$$remotePort(9876);changed.d.set_io$$initialSequence(30000);
 changed.d.set_io$$localMss(512);changed.d.set_io$$passive(0);changed.d.set_io$$openMask(2);changed.step();
 changed.d.set_io$$localPort(changed.localPort[0]);changed.d.set_io$$remoteIp(changed.remoteIp[0]);
 changed.d.set_io$$remotePort(changed.remotePort[0]);changed.idle(20);
 check(changed.state(1)==0&&!(changed.d.get_io$$openReady()&2)&&(changed.d.get_io$$openConflict()&2),
       "changed OPEN used eligibility for a different tuple");changed.d.set_io$$openMask(0);
 std::cout<<"multi listeners: shared port, bad/duplicate SYN, RST return and duplicate active OPEN passed\n";
}
static void passiveOwnership(){
 Peer p;p.random=false;p.open(0,210000,true,512);p.autoComplete=false;
 p.peerNext[0]=220001;p.rx(0,220000,0,2);
 p.until([&]{return !p.pending.empty();});
 check(p.state(0)==3&&p.pending.front().bytes[33]==18,"SYNACK must be pending physical completion");
 auto old=p.pending.front().meta;
 p.wire(p.encode(0,p.peerNext[0],0,4));
 auto corrupt=p.encode(0,p.peerNext[0],0);corrupt[36]^=1;p.wire(corrupt);p.idle(160);
 check(p.state(0)==1&&p.pending.size()==1,"RST must return to LISTEN with old SYNACK still owned");
 p.d.set_io$$appConnection(1);p.d.set_io$$remoteIp(p.remoteIp[0]);p.d.set_io$$remotePort(p.remotePort[0]);
 p.d.set_io$$localPort(p.localPort[0]);p.d.set_io$$initialSequence(230000);p.d.set_io$$localMss(512);
 p.d.set_io$$passive(0);p.d.set_io$$openMask(2);
 p.step();
 check(!(p.d.get_io$$openReady()&2)&&(p.d.get_io$$openConflict()&2),
       "LISTEN rollback released tuple before old SYNACK physical completion");
 p.idle(20);check(p.state(1)==0,"duplicate active tuple opened while old SYNACK owned");
 p.d.set_io$$openMask(0);p.autoComplete=true;p.drain();
 p.remoteIp[1]=p.remoteIp[0];p.remotePort[1]=p.remotePort[0];
 p.active(1,230000,240000,512);check(p.state(1)==4,"drained LISTEN tuple cannot move to other slot");
 auto invalid=p.invalid;p.injection=MultiResult{old.tag,old.gen};p.step();
 check(p.invalid==invalid+1&&p.state(1)==4,"late old SYNACK callback affected migrated tuple");
 std::cout<<"passive SYNACK/RST ownership barrier and post-drain tuple migration passed\n";
}
static void completionAndReuse(){
 Peer p;activeFour(p);p.random=false;p.autoComplete=false;
 p.write(0,79);p.write(1,81);p.until([&]{return p.pending.size()>=2;});
 auto old=p.pending.front().meta;auto second=p.pending[1].meta;
 auto before=p.invalid;p.injection=MultiResult{second.tag,second.gen};p.step();
 p.injection=MultiResult{old.tag,old.gen+1};p.step();p.injection=MultiResult{old.tag+777,old.gen};p.step();
 check(p.invalid==before+3&&p.pending.size()>=2,"global completion order/generation/tag validation failed");
 p.stallOutput=true;p.close(0);p.idle(40);
 p.d.set_io$$appConnection(0);p.d.set_io$$openMask(1);p.d.set_io$$passive(0);
 p.d.set_io$$remoteIp(p.remoteIp[0]);p.d.set_io$$remotePort(p.remotePort[0]);p.d.set_io$$localPort(p.localPort[0]);
 p.idle(20);check(!(p.d.get_io$$openReady()&1),"reopen bypassed old MAC ownership");
 p.d.set_io$$openMask(0);p.stallOutput=false;p.autoComplete=true;p.drain();
 p.ack(1);p.active(0,91000,92000,512);check(p.generation[0]!=old.gen,"generation not advanced on reuse");
 before=p.invalid;p.injection=MultiResult{old.tag,old.gen};p.step();check(p.invalid==before+1&&p.state(0)==4,"old generation completion affected reopened context");
 // An admitted old RX packet must drain before OPEN can observe a new generation.
 p.stallReceiveMask=4;auto v=p.pattern(73);p.expect(2,v);auto seq=p.peerNext[2];p.peerNext[2]+=v.size();
 p.wire(p.encode(2,seq,p.planned[2],24,65535,v));p.close(2);
 p.expectedRx[2].clear();p.stallReceiveMask=0;p.drain();p.active(2,93000,94000,512);
 p.idle(200);check(p.expectedRx[2].empty()&&p.state(2)==4,"old receive pipeline escaped into reused context");
 // A physical failure must abort only the ledger's owner.
 p.autoComplete=false;p.write(3,77);p.write(2,55);p.until([&]{return p.pending.size()>=2;});
 auto failed=p.pending.front().meta;p.injection=MultiResult{failed.tag,failed.gen,true};p.step();
 p.autoComplete=true;p.idle(200);p.drain();
 check(p.state(failed.connection)==0,"physical failure did not abort its owner");
 for(unsigned c=0;c<4;++c)if(c!=failed.connection)check(p.state(c)==4,"physical failure aborted a different connection");
 // A CLOSED slot still owns its tuple until old frames/completions drain.
 Peer q;q.random=false;q.active(0,120000,130000,512);q.autoComplete=false;
 q.write(0,79);q.until([&]{return !q.pending.empty();});q.close(0);
 q.d.set_io$$appConnection(1);q.d.set_io$$remoteIp(q.remoteIp[0]);q.d.set_io$$remotePort(q.remotePort[0]);
 q.d.set_io$$localPort(q.localPort[0]);q.d.set_io$$initialSequence(140000);q.d.set_io$$localMss(512);
 q.d.set_io$$passive(0);q.d.set_io$$openMask(2);q.idle(20);
 check((q.d.get_io$$openConflict()&2)&&!(q.d.get_io$$openReady()&2),"another slot reused a tuple before old MAC ownership drained");
 q.d.set_io$$openMask(0);q.autoComplete=true;q.drain();
 q.remoteIp[1]=q.remoteIp[0];q.remotePort[1]=q.remotePort[0];q.active(1,140000,150000,512);
 check(q.state(1)==4,"drained tuple cannot move to a different connection ID");
 std::cout<<"multi completion: global ordering, wrong tags/generations, abort drain/reuse and RX generation barrier passed\n";
}
static void parallelSack(){
 Peer p;p.random=false;
 for(unsigned c=0;c<2;++c){
  p.open(c,300000+c*10000,false,256);p.until([&]{return p.d.get_io$$sendActual()==p.planned[c];});
  p.peerNext[c]=400001+c*10000;
  p.wire(p.encode(c,p.peerNext[c]-1,p.planned[c],18,65535,{}, {2,4,1,0,4,2,1,1}));p.idle(180);
  check(p.state(c)==4,"SACK-negotiated context did not establish");
 }
 std::array<std::vector<uint8_t>,2> data{p.pattern(97),p.pattern(173)};
 std::array<unsigned,2> gap{31,67};std::array<uint32_t,2> base{p.peerNext[0],p.peerNext[1]};
 for(unsigned c=0;c<2;++c){
  p.expect(c,data[c]);std::vector<uint8_t> tail(data[c].begin()+gap[c],data[c].end());
  p.wire(p.encode(c,base[c]+gap[c],p.planned[c],24,65535,tail));
 }p.idle(800);
 std::array<bool,2> sackSeen{};
 for(auto&packet:p.packets){
  unsigned c=packet.meta.connection;if(c>=2)continue;
  auto&v=packet.bytes;unsigned end=20+(v[32]>>4)*4;
  for(unsigned off=40;off<end;){
   unsigned kind=v[off];if(!kind)break;if(kind==1){++off;continue;}
   check(off+1<end&&v[off+1]>=2&&off+v[off+1]<=end,"bad outbound option encoding");
   if(kind==5&&v[off+1]>=10){
    check(u32(v,off+2)==base[c]+gap[c]&&u32(v,off+6)==base[c]+data[c].size(),"SACK block leaked between connections");
    check(u32(v,28)==base[c],"SACK advanced cumulative ACK over a hole");sackSeen[c]=true;
   }off+=v[off+1];
  }
 }
 check(sackSeen[0]&&sackSeen[1],"both contexts did not report independent SACK blocks");
 for(unsigned c:{1U,0U}){
  std::vector<uint8_t> head(data[c].begin(),data[c].begin()+gap[c]);
  p.peerNext[c]+=data[c].size();p.wire(p.encode(c,base[c],p.planned[c],24,65535,head));p.idle(400);
  check(p.expectedRx[c].empty(),"SACK context reassembly failed");
 }p.drain();
 std::cout<<"multi SACK: two negotiated sessions report independent gaps and reassemble independently\n";
}
static void transmitService(){
 Peer p;p.random=false;activeFour(p);
 for(unsigned activeCount:{4U,1U}){
  auto counts=p.dataPackets;unsigned first=p.packets.size();p.stallOutput=true;
  for(unsigned round=0;round<4;++round)for(unsigned c=0;c<activeCount;++c)p.write(c,1);
  p.idle(60);p.stallOutput=false;
  p.until([&]{for(unsigned c=0;c<activeCount;++c)if(p.dataPackets[c]<counts[c]+4)return false;return true;});
  p.drain();std::vector<unsigned> end;
  for(unsigned i=first;i<p.packets.size();++i)if(p.packets[i].meta.data)end.push_back(p.packets[i].cycle);
  check(end.size()==activeCount*4,"minimum TX burst packet count changed");
  unsigned elapsed=end.back()-end.front(),intervals=end.size()-1;
  check(elapsed*2<=intervals*21,"minimum TCP TX below 10G service");
  std::cout<<"multi minimum 1B TX ("<<activeCount<<" active): "<<elapsed<<"/"<<intervals<<" cycles, "<<double(elapsed)/intervals<<" cycles/packet\n";
  for(unsigned c=0;c<activeCount;++c)p.ack(c);p.drain();
 }
 Peer q;q.random=false;
 for(unsigned c=0;c<4;++c)q.active(c,510000+c*10000,610000+c*10000,1460);
 q.stallOutput=true;
 for(unsigned c=0;c<4;++c)q.write(c,1460);
 q.idle(100);unsigned first=q.packets.size();q.stallOutput=false;q.until([&]{return std::all_of(q.dataPackets.begin(),q.dataPackets.end(),[](unsigned n){return n>=1;});});q.drain();
 std::vector<unsigned>end;for(unsigned i=first;i<q.packets.size();++i)if(q.packets[i].meta.data)end.push_back(q.packets[i].cycle);
 check(end.size()==4,"large TX burst count");
 unsigned elapsed=end.back()-end.front();check(elapsed*4<=3*769,"1460B shared TX below 10G service");
 std::cout<<"multi 1460B TX: "<<elapsed<<"/3 cycles, "<<double(elapsed)/3<<" cycles/packet\n";
}
static void fairnessAndService(){
 Peer p;p.random=false;activeFour(p);
 p.stallOutput=true;
 for(unsigned c=0;c<4;++c)p.write(c,1460);p.idle(60);
 unsigned start=p.packets.size();p.stallOutput=false;p.drain();
 std::array<unsigned,4> seen{};unsigned count=0;
 for(unsigned i=start;i<p.packets.size()&&count<4;++i)if(p.packets[i].meta.data){++seen[p.packets[i].meta.connection];++count;}
 check(count==4&&std::all_of(seen.begin(),seen.end(),[](unsigned n){return n==1;}),"packet arbitration starved a ready connection");
 for(unsigned c=0;c<4;++c)p.ack(c);p.drain();
 // Fixed-rate receive test includes link preamble/FCS/IFG allowance: a minimum
 // Ethernet TCP frame has a 10.5-cycle budget at 156.25 MHz for 10 Gbit/s.
 auto before=p.drops;unsigned begin=p.cycles;
 constexpr unsigned N=128;
 for(unsigned n=0;n<N;++n){
  unsigned deadline=begin+((n+1)*21)/2;
  unsigned c=n%4;p.wire(p.encode(c,p.peerNext[c],p.planned[c]));
  check(p.cycles<=deadline,"minimum packet source missed 10G deadline");while(p.cycles<deadline)p.step();
 }
 unsigned elapsed=p.cycles-begin;p.idle(400);
 check(p.drops==before,"minimum-packet 10G receive offered load dropped packets");
 std::cout<<"multi minimum RX: "<<elapsed<<"/"<<N<<" cycles, "<<double(elapsed)/N<<" cycles/packet, zero drops at 10G wire budget\n";
 // The same ingress service bound must also hold with only one active tuple.
 begin=p.cycles;before=p.drops;
 for(unsigned n=0;n<N;++n){
  unsigned deadline=begin+((n+1)*21)/2;p.wire(p.encode(0,p.peerNext[0],p.planned[0]));
  check(p.cycles<=deadline,"minimum packet source missed 10G deadline");while(p.cycles<deadline)p.step();
 }p.idle(400);check(p.drops==before,"single active context minimum-packet 10G offered load dropped packets");

 // Minimum-sized Ethernet data frames must also service the RX ring and ACK path.
 for(unsigned activeCount:{4U,1U}){
  begin=p.cycles;before=p.drops;
  for(unsigned n=0;n<64;++n){
   unsigned deadline=begin+((n+1)*21)/2,c=n%activeCount;
   auto v=p.pattern(1);p.expect(c,v);auto seq=p.peerNext[c]++;
   p.wire(p.encode(c,seq,p.planned[c],24,65535,v));
   check(p.cycles<=deadline,"minimum data source missed 10G deadline");while(p.cycles<deadline)p.step();
  }p.idle(1000);
  check(p.drops==before,"minimum data receive dropped at 10G");
  for(unsigned c=0;c<4;++c)check(p.expectedRx[c].empty(),"minimum data receive did not retire every byte");
  std::cout<<"multi minimum 1B RX ("<<activeCount<<" active): 10.5 cycles/packet, zero drops and all bytes delivered\n";
 }
 // Large payload is striped across contexts, with independent expected streams.
 // One 1460-byte TCP payload occupies 1538 wire bytes including link overhead.
 for(unsigned c=0;c<4;++c)p.mss[c]=1460;
 begin=p.cycles;before=p.drops;
 constexpr unsigned Large=32;
 for(unsigned n=0;n<Large;++n){
  unsigned deadline=begin+((n+1)*769)/4; // exact 192.25 cycles/1538 wire bytes
  unsigned c=n%4;auto v=p.pattern(1460);p.expect(c,v);auto seq=p.peerNext[c];p.peerNext[c]+=v.size();
  p.wire(p.encode(c,seq,p.planned[c],24,65535,v));check(p.cycles<=deadline,"large-packet source missed 10G deadline");while(p.cycles<deadline)p.step();
 }
 elapsed=p.cycles-begin;p.idle(1500);
 check(p.drops==before,"large-packet 10G RX dropped packets");
 for(unsigned c=0;c<4;++c)check(p.expectedRx[c].empty(),"large-packet receive stream incomplete");
 std::cout<<"multi 1460B RX: "<<elapsed<<"/"<<Large<<" cycles, "<<double(elapsed)/Large<<" cycles/packet, zero drops at 10G wire budget\n";
}
int main(){try{independentData();listeners();passiveOwnership();completionAndReuse();parallelSack();transmitService();fairnessAndService();
 std::cout<<"four-context TCP isolation/ownership/fairness/service checks passed\n";return 0;
}catch(const std::exception&e){std::cerr<<"tcp_multi: "<<e.what()<<'\n';return 1;}}
