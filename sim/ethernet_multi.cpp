#include "MultiEthernetTcpHarness.h"
#include "multi_tcp_peer.h"
using Peer=MultiTcpPeer<SMultiEthernetTcpHarness,true>;
static std::vector<uint8_t> ipPacket(uint32_t source,unsigned protocol,const std::vector<uint8_t>&body){
 std::vector<uint8_t>ip(20);ip[0]=0x45;put16(ip,2,20+body.size());ip[8]=64;ip[9]=protocol;
 put32(ip,12,source);put32(ip,16,0x0a000001);put16(ip,10,sum(ip));ip.insert(ip.end(),body.begin(),body.end());return ip;
}
static void transmitLoad(unsigned payload){
 Peer p;p.random=false;
 for(unsigned c=0;c<4;++c){p.learn(c);p.active(c,710000+c*10000,810000+c*10000,1460);}
 constexpr unsigned N=256,Prefill=16;
 unsigned first=p.packets.size();auto before=p.dataPackets;
 p.stallOutput=true;
 // Four banks per context are filled before service measurement starts.
 for(unsigned n=0;n<Prefill;++n)p.write(n%4,payload);
 p.idle(100);p.autoAcknowledge=true;p.stallOutput=false;
 // write() keeps valid asserted until handshake while step() independently
 // supplies peer ACKs, consumes MAC output and returns physical completions.
 for(unsigned n=Prefill;n<N;++n)p.write(n%4,payload);
 p.until([&]{unsigned count=0;for(unsigned c=0;c<4;++c)count+=p.dataPackets[c]-before[c];return count==N;},200000);
 p.until([&]{return p.pending.empty()&&p.automaticInput.empty()&&!p.d.get_io$$outstanding();});p.idle(400);
 std::vector<unsigned>end;
 for(unsigned i=first;i<p.packets.size();++i)if(p.packets[i].meta.data)end.push_back(p.packets[i].cycle);
 check(end.size()==N,"continuous Ethernet data packet count mismatch");
 unsigned elapsed=end.back()-end.front(),intervals=N-1;
 if(payload==1)check(elapsed*2<=intervals*21,"minimum Ethernet TCP TX below 10G sustained service");
 else check(elapsed*4<=intervals*769,"1460B Ethernet TCP TX below 10G sustained service");
 for(unsigned c=0;c<4;++c){
  p.select(c);check(p.d.get_io$$freeBanks()==4&&p.d.get_io$$sendOldest()==p.planned[c],"continuous peer ACK did not reclaim every bank");
 }
 check(!p.drops,"continuous full-duplex TCP service dropped peer ACKs");
 std::cout<<"Ethernet four-context "<<payload<<"B TCP TX: "<<elapsed<<"/"<<intervals<<" cycles = "<<double(elapsed)/intervals
  <<" cycles/packet, 256 frames, startup/drain excluded, ACK queue peak="<<p.maxAckQueue<<", all banks cumulatively ACKed\n";
}
int main(){try{
 Peer p;
 for(unsigned c=0;c<4;++c)p.learn(c);
 for(unsigned c=0;c<4;++c)p.open(c,10000+c*1000,false,512);
 p.idle(800);check(p.d.get_io$$states()==0x2222,"Ethernet four active SYNs not submitted");
 for(unsigned c:{2U,0U,3U,1U}){p.peerNext[c]=20001+c*1000;p.rx(c,p.peerNext[c]-1,p.planned[c],18);}
 check(p.d.get_io$$states()==0x4444,"Ethernet four handshakes failed");p.drain();
 p.stallReceiveMask=1;p.receive(0,p.pattern(900));
 for(unsigned c=1;c<4;++c){p.receive(c,p.pattern(71+c));p.write(c,261+c*11);}

 // Also block connection 0's control notice and fill its bounded ingress queue.
 p.d.set_io$$noticeReadyMask(14);p.rx(0,p.peerNext[0],p.planned[0],17);++p.peerNext[0];
 auto busy=p.busyDrops;
 for(unsigned n=0;n<12;++n)p.wire(p.encode(0,p.peerNext[0],p.planned[0]),false,0);
 p.idle(100);check(p.busyDrops>busy,"Ethernet busy-context drop not exercised");
 // Share ARP/UDP/ICMP while TCP connection 0 retains unread application bytes.
 auto payload=p.pattern(61);std::vector<uint8_t>udp(8);put16(udp,0,1200);put16(udp,2,1300);put16(udp,4,8+payload.size());
 udp.insert(udp.end(),payload.begin(),payload.end());
 p.wire(ipPacket(p.remoteIp[1],17,udp),false,1);p.idle(400);
 check(p.udpBytes==payload,"shared UDP receive failed or blocked by stalled TCP context");
 auto echo=p.pattern(32);echo[0]=8;echo[1]=0;echo[2]=echo[3]=0;put16(echo,2,sum(echo));
 p.wire(ipPacket(p.remoteIp[2],1,echo),false,2);p.idle(600);check(p.icmpFrames==1,"shared ICMP engine not independent");
 p.d.set_io$$noticeReadyMask(15);p.idle(100);p.learn(3);p.drain();
 for(unsigned c=1;c<4;++c){check(p.expectedRx[c].empty(),"Ethernet RX context isolation");p.ack(c);}
 p.stallReceiveMask=0;p.until([&]{return p.expectedRx[0].empty();});p.drain();
 // Check MAC-boundary tags remain distinct even when per-context local tags match.
 p.autoComplete=false;p.write(0,63);p.write(3,83);p.until([&]{return p.pending.size()>=2;});
 auto first=p.pending[0].meta,second=p.pending[1].meta;check(first.tag!=second.tag,"global Ethernet tags collided");
 unsigned invalid=p.invalid;p.injection=MultiResult{second.tag,second.gen};p.step();check(p.invalid==invalid+1,"cross-context out-of-order MAC callback accepted");
 p.autoComplete=true;p.drain();p.ack(0);p.ack(3);p.drain();
 auto drops=p.drops;auto bad=p.encode(2,p.peerNext[2],p.planned[2],24,65535,{1,2,3,4});bad.back()^=0x80;
 p.wire(bad,false,2);p.idle(400);check(p.drops>drops&&p.d.get_io$$states()==0x4447,"bad Ethernet TCP packet corrupted shared contexts");
 std::cout<<"Ethernet multi: four TCP contexts with shared ARP/UDP/ICMP, stalled RX isolation, wire checksums and global MAC completion tags passed\n";
 transmitLoad(1);transmitLoad(1460);
 return 0;
}catch(const std::exception&e){std::cerr<<"ethernet_multi: "<<e.what()<<'\n';return 1;}}
