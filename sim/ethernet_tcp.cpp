#include "EthernetTcpHarness.h"
#include "ethernet_peer.h"
using Test=EthernetPeer<SEthernetTcpHarness>;
int main(){try{
 Test t;
 // Submission does not start RTO. Wrong, stale and duplicate completions
 // cannot free a descriptor or mutate the transmitted sequence.
 t.autoComplete=false;t.open(0xfffffff0);t.until([&]{return t.packets.size()==1;});
 auto syn=t.packets.back();t.idle(60,true);
 check(!t.d.get_io$$timerRunning()&&t.d.get_io$$sendActual()==0xfffffff0,"queued SYN started timer");
 t.injection=Result{syn.meta.tag+1,syn.meta.gen};t.step();
 t.injection=Result{syn.meta.tag,syn.meta.gen+1};t.step();check(t.invalid==2,"wrong completion accepted");
 t.injection=Result{syn.meta.tag,syn.meta.gen};t.step();t.idle(2);
 check(t.d.get_io$$timerRunning(),"physical SYN completion did not arm timer");
 t.injection=Result{syn.meta.tag,syn.meta.gen};t.step();check(t.invalid==3,"duplicate completion accepted");
 size_t np=t.packets.size();t.idle(11,true);t.until([&]{return t.packets.size()>np;});
 check(t.packets.back().meta.retry,"SYN timeout did not retransmit");
 t.autoComplete=true;t.drain();t.peerNext=1001;t.rx(1000,0xfffffff1,18);t.drain();
 check(t.d.get_io$$state()==4,"handshake after SYN loss");
 // MSS segmentation, sequence wrap, partial ACK then byte-exact retransmit.
 uint32_t base=t.planned;t.write(2048);t.until([&]{return t.d.get_io$$sendActual()==t.planned;});
 t.ack(base+37);unsigned retries=t.replayed;np=t.packets.size();t.idle(t.d.get_io$$rto()+1,true);
 t.until([&]{return t.replayed>retries;});
 auto retry=t.packets.back();check(u32(retry.bytes,24)==base+37&&retry.meta.retry,"partial-ACK replay start");
 t.drain();t.ack(t.planned);t.until([&]{return t.d.get_io$$freeBanks()==4;});
 // Fill the completion ledger. The fifth packet cannot consume hidden space.
 t.autoComplete=false;t.write(2048);t.write(2048);t.write(2048);t.idle(500);
 check(t.d.get_io$$outstanding()==4&&t.pending.size()==4,"completion ledger did not apply backpressure");
 auto wrong=t.pending.back();t.injection=Result{wrong.meta.tag,wrong.meta.gen};t.step();
 check(t.invalid==4&&t.pending.size()==4,"out-of-order completion consumed ledger head");
 t.autoComplete=true;t.until([&]{return t.d.get_io$$sendActual()==t.planned;});t.ack(t.planned);t.drain();
 // Graceful close waits for unscheduled writes and actual data completion.
 t.write(2048);t.close();check(t.d.get_io$$state()==5,"graceful close state");
 t.until([&]{return t.d.get_io$$sendActual()==uint32_t(t.planned+1);});
 check((t.packets.back().bytes[33]&1)&&u32(t.packets.back().bytes,24)==t.planned,"FIN overtook application bytes");
 t.rx(t.peerNext,t.planned+1,17);t.drain();check(t.d.get_io$$state()==10,"TIME-WAIT missing");
 t.idle(25,true);t.idle(10);check(t.d.get_io$$state()==0,"TIME-WAIT did not expire");
 // Abort must preserve an already offered data beat, cancel remaining data,
 // drain callbacks and allow reuse without old payload/descriptor leakage.
 t.active(5000,6000,256);t.stallOutput=true;t.write(1024);t.until([&]{return t.d.get_io$$outValid();});
 t.close(true);check(t.d.get_io$$state()==0&&t.d.get_io$$outValid(),"abort destroyed offered frame");
 t.autoComplete=false;t.stallOutput=false;t.idle(300);t.d.set_io$$openValid(1);t.idle(20);
 check(!t.d.get_io$$openReady(),"reopen accepted with old physical completions pending");
 t.d.set_io$$openValid(0);t.autoComplete=true;t.drain();t.idle(20);check(t.d.get_io$$freeBanks()==4,"abort leaked payload banks");
 // Routing/MAC failure aborts rather than advancing SND.NXT.
 t.autoComplete=false;t.open(8000);t.until([&]{return !t.pending.empty();});auto fail=t.pending.front();
 t.injection=Result{fail.meta.tag,fail.meta.gen,true};t.step();t.idle(50);
 check(t.d.get_io$$state()==0&&t.d.get_io$$sendActual()==8000,"failed SYN counted as transmitted");
 t.autoComplete=true;t.drain();
 // Whole-IP receive correctness: checksum/MAC error rejection, overlap,
 // gaps, durable buffering, zero-window update and packed short segments.
 t.active(10000,20000,1460);
 auto bad=t.encode(t.peerNext,t.planned,24,65535,{1,2,3,4});bad[14+u16(bad,16)-1]^=1;
 unsigned beforeDrops=t.drops;t.wire(bad);t.idle(200);
 check(t.drops==beforeDrops+1&&t.d.get_io$$receiveNext()==t.peerNext,"bad TCP checksum reached receive state");
 bad=t.encode(t.peerNext,t.planned,24,65535,{1,2,3});t.wire(bad,true);t.idle(200);
 check(t.drops==beforeDrops+2,"late stream error escaped IPv4 commit");
 t.wire(t.encode(t.peerNext+8,t.planned,24,65535,{1,2,3}));t.idle(200);
 check(t.d.get_io$$receiveNext()==t.peerNext,"gap advanced receive sequence");
 std::vector<uint8_t> block(700);for(auto&v:block)v=t.rng();t.receiveData(block);
 // Prefix overlap is deliberately >127 bytes: TCP trimming is 16-bit.
 block.resize(900);for(auto&v:block)v=t.rng();t.receiveData(block,300);
 t.stallReceive=true;block.resize(1024);for(auto&v:block)v=t.rng();t.receiveData(block);t.drain();
 check(t.d.get_io$$receiveWindow()==0&&u16(t.packets.back().bytes,34)==0,"full receive ring did not advertise zero window");
 uint32_t nxt=t.peerNext;t.wire(t.encode(nxt,t.planned,24,65535,{99}));t.idle(200);
 check(t.d.get_io$$receiveNext()==nxt,"zero-window data was acknowledged");
 t.stallReceive=false;t.until([&]{return t.expectedRx.empty();});t.idle(200);t.drain();
 check(t.d.get_io$$receiveWindow()==1024&&u16(t.packets.back().bytes,34)==1024,"application drain did not reopen window");
 for(unsigned i=0;i<100;++i){block.resize(1+i%17);for(auto&v:block)v=t.rng();t.receiveData(block);}
 check(t.expectedRx.empty(),"received stream did not drain");
 t.close(true);t.drain();
 // A fresh ARP lookup can fail without producing a TCP wire frame.
 t.d.set_io$$flushArp(1);t.idle(2);t.d.set_io$$flushArp(0);t.answerArp=false;
 unsigned arpBefore=t.arpRequests;t.open(30000);t.idle(300,true);t.idle(100);
 check(t.arpRequests==arpBefore+3&&t.d.get_io$$state()==0&&t.d.get_io$$sendActual()==30000,"ARP exhaustion did not fail TCP submission");
 t.answerArp=true;t.drain();
 // Data carried on a passive SYN is durable but hidden until the
 // final handshake ACK; it must then be delivered exactly once.
 t.open(35000,true);std::vector<uint8_t> early(200);for(auto&v:early)v=t.rng();
 unsigned beforeEarly=t.receivedBytes;t.peerNext=45201;for(auto v:early)t.expectedRx.push_back(v);
 t.wire(t.encode(45000,0,2,65535,early));t.idle(400);t.until([&]{return t.d.get_io$$sendActual()==35001;});
 check(t.d.get_io$$state()==3&&t.receivedBytes==beforeEarly&&u32(t.packets.back().bytes,28)==45201,"SYN payload exposed before handshake or not ACKed durably");
 t.rx(45201,35001);t.until([&]{return t.expectedRx.empty();});check(t.receivedBytes==beforeEarly+200,"SYN payload delivery");
 t.close(true);t.drain();
 // Graceful close must retain bytes the application has not read yet.
 t.active(40000,50000);t.stallReceive=true;block.resize(100);for(auto&v:block)v=t.rng();t.receiveData(block);
 t.close();t.until([&]{return t.d.get_io$$sendActual()==uint32_t(t.planned+1);});
 t.rx(t.peerNext,t.planned+1,17);t.drain();t.idle(25,true);t.idle(10);
 check(t.d.get_io$$state()==0&&t.d.get_io$$receiveWindow()==924,"graceful close discarded unread bytes");
 t.stallReceive=false;t.until([&]{return t.expectedRx.empty();});t.idle(20);
 check(t.d.get_io$$receiveWindow()==1024,"closed connection could not drain application data");
 // Passive handshake + many generations and randomized downstream stalls.
 for(unsigned k=0;k<24;++k){
  uint32_t seq=uint32_t(t.rng()),peer=uint32_t(t.rng());t.open(seq,true,128+(k%5)*127);
  t.peerNext=peer+1;t.rx(peer,0,2);t.until([&]{return t.d.get_io$$sendActual()==seq+1;});
  t.rx(peer+1,seq+1);check(t.d.get_io$$state()==4,"passive handshake failed");
  for(unsigned j=0;j<4;++j){t.write(1+t.rng()%2048);t.until([&]{return t.d.get_io$$sendActual()==t.planned;});
   if(j==1){unsigned prior=t.replayed;t.idle(t.d.get_io$$rto()+1,true);t.until([&]{return t.replayed>prior;});t.drain();}
   t.ack(t.planned);
  }
  t.close(true);t.drain();
 }
 std::cout<<"ETHERNET_TCP_ENDPOINT_PASS cycles="<<t.cycles<<" opens="<<t.opens<<" writes="<<t.stored
  <<" data_packets="<<t.dataPackets<<" retransmissions="<<t.replayed<<" controls="<<t.controls
  <<" arp_requests="<<t.arpRequests<<" received_bytes="<<t.receivedBytes<<" rx_drops="<<t.drops<<" completions="<<t.completed<<" rejected_completions="<<t.invalid
  <<" wire_checksums=pass partial_ack=pass ledger_backpressure=pass abort_stability=pass route_failure=pass reuse=pass\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
