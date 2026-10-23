#include "MultiTcpEndpointHarness.h"
#include "multi_tcp_peer.h"
using Peer=MultiTcpPeer<SMultiTcpEndpointHarness>;
int main(){try{
 Peer p;p.random=false;p.open(0,210000,true,512);p.autoComplete=false;
 p.peerNext[0]=220001;p.rx(0,220000,0,2);
 p.until([&]{return !p.pending.empty();});
 check(p.state(0)==3&&p.pending.front().bytes[33]==18,"SYNACK must be pending physical completion");
 auto old=p.pending.front().meta;
 p.rx(0,p.peerNext[0],0,4);
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
 return 0;
}catch(const std::exception&e){std::cerr<<"passive_ownership: "<<e.what()<<'\n';return 1;}}
