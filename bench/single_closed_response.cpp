#include "EthernetServiceHarness.h"
#include "gsim_model_init.h"
#include "ethernet_peer.h"
struct InitializedModel : SEthernetServiceHarness {
 InitializedModel(){initializeGsimModel(*this,&SEthernetServiceHarness::init);}
};
using Peer=EthernetPeer<InitializedModel>;
static void repairTcp(std::vector<uint8_t>&eth){
 const unsigned ip=14,tcp=34,n=u16(eth,ip+2)-20;
 Peer::put16(eth,tcp+16,0);std::vector<uint8_t> pseudo(eth.begin()+ip+12,eth.begin()+ip+20);
 pseudo.push_back(0);pseudo.push_back(6);pseudo.push_back(n>>8);pseudo.push_back(n);
 pseudo.insert(pseudo.end(),eth.begin()+tcp,eth.begin()+tcp+n);Peer::put16(eth,tcp+16,sum(pseudo));
}
static std::vector<uint8_t> unknown(Peer&p,uint32_t seq,uint32_t ack,unsigned flags,
 unsigned localPort=6000,unsigned remotePort=443,const std::vector<uint8_t>&payload={}){
 auto eth=p.encode(seq,ack,flags,65535,payload);Peer::put16(eth,34,remotePort);
 Peer::put16(eth,36,localPort);repairTcp(eth);return eth;
}
static void refill(Peer&p){p.idle(12,true);p.idle(30);}
static void checkException(const std::vector<uint8_t>&ip,unsigned srcPort,unsigned dstPort,
 unsigned flags,uint32_t seq,uint32_t ack){
 check(u16(ip,20)==srcPort&&u16(ip,22)==dstPort,"exception port reversal");
 check(ip[33]==flags&&u32(ip,24)==seq&&u32(ip,28)==ack,"exception sequence/ACK");
}
static void closed(){
 Peer p;p.random=false;
 p.wire(unknown(p,0xffffffffU,0,2));p.idle(300);
 check(p.exceptions.size()==1,"closed unknown tuple did not produce reset");
 checkException(p.exceptions.back(),6000,443,20,0,0);
 check(p.d.get_io$$state()==0&&p.d.get_io$$outstanding()==0&&p.pending.empty()&&p.packets.empty()&&
       p.completed==0&&p.tcpRoutes==0,"stateless CLOSED reset entered completion ledger or connection state");
 refill(p);p.wire(unknown(p,100,0x12345678,16,6001,444));p.idle(300);
 check(p.exceptions.size()==2,"unknown ACK did not produce reset");
 checkException(p.exceptions.back(),6001,444,4,0x12345678,0);
 refill(p);p.wire(unknown(p,300,100,4));p.idle(300);
 check(p.exceptions.size()==2,"incoming RST produced an exception");
 refill(p);auto bad=unknown(p,400,0,2);bad[50]^=1;p.wire(bad);p.idle(300);
 check(p.exceptions.size()==2,"bad TCP checksum produced an exception");
 check(p.d.get_io$$state()==0&&p.d.get_io$$outstanding()==0&&p.pending.empty()&&p.tcpRoutes==0,
       "suppression damaged connection state/ledger");
 std::cout<<"SINGLE_CLOSED unknown_syn=pass ack_reset=pass rst_suppression=pass checksum=pass no_ledger=pass cycles="<<p.cycles<<"\n";
}
static void activeStall(){
 Peer p;p.random=false;p.active(10000,20000);p.drain();
 const auto generation=p.d.get_io$$generation(),base=p.d.get_io$$sendActual();
 const auto realBefore=p.packets.size();const unsigned routesBefore=p.tcpRoutes,completedBefore=p.completed;
 p.autoComplete=false;p.stallOutput=true;
 std::vector<uint8_t> noise(57,0xe7);
 p.wire(unknown(p,0xffffffe0U,0x24681357,24,12345,444,noise));p.idle(220);
 check(p.d.get_io$$outValid()&&!p.d.get_io$$outTcp(),"exception stall was not exercised");
 check(p.packets.size()==realBefore&&p.pending.empty()&&p.d.get_io$$outstanding()==0&&p.tcpRoutes==routesBefore,
       "stalled exception consumed connection metadata");
 const uint32_t start=p.peerNext;std::vector<uint8_t> payload(93);
 for(unsigned i=0;i<payload.size();++i)payload[i]=uint8_t(i*17+3);
 for(auto b:payload)p.expectedRx.push_back(b);p.peerNext+=payload.size();
 p.wire(p.encode(start,p.planned,24,65535,payload));p.idle(200);
 check(p.expectedRx.empty()&&p.receivedBytes==payload.size()&&p.d.get_io$$receiveNext()==p.peerNext,
       "stalled exception blocked or contaminated valid receive");
 p.write(127);p.idle(160);
 check(p.d.get_io$$sendActual()==base&&p.d.get_io$$generation()==generation&&p.d.get_io$$state()==4,
       "stalled exception changed transmitted sequence or generation");
 check(p.d.get_io$$outValid()&&!p.d.get_io$$outTcp(),"real data displaced stalled exception");
 p.stallOutput=false;p.until([&]{return p.dataPackets==1&&p.exceptions.size()==1;});p.idle(300);
 checkException(p.exceptions.back(),12345,444,4,0x24681357,0);
 check(p.receivedBytes==93&&p.d.get_io$$state()==4&&p.d.get_io$$generation()==generation,
       "unknown payload contaminated live state");
 const unsigned realSent=p.packets.size()-realBefore;
 check(realSent>=1&&p.pending.size()==realSent&&p.d.get_io$$outstanding()==realSent&&
       p.tcpRoutes-routesBefore==realSent&&p.completed==completedBefore&&p.invalid==0,
       "exception stole or created a real completion descriptor");
 p.autoComplete=true;p.drain();check(p.completed-completedBefore==realSent,"wrong physical completions after exception");
 check(p.d.get_io$$sendActual()==p.planned,"real payload did not advance physical send extent");
 p.ack(p.planned);p.drain();
 check(p.d.get_io$$sendOldest()==p.planned&&p.d.get_io$$freeBanks()==8,"real data retirement failed after exception");
 p.write(65);p.until([&]{return p.dataPackets==2;});p.drain();p.ack(p.planned);p.drain();
 check(p.d.get_io$$state()==4&&p.d.get_io$$sendOldest()==p.planned&&p.invalid==0,"live connection broken after exception");
 std::cout<<"SINGLE_ACTIVE_STALL held_exception=pass rx_progress=pass payload_discard=pass tx_metadata=pass completion_isolation=pass real_packets="<<realSent<<" cycles="<<p.cycles<<"\n";
}
static void listener(){
 Peer p;p.random=false;p.d.set_io$$remoteIp(0);p.d.set_io$$remotePort(0);p.open(30000,true);
 check(p.d.get_io$$state()==1,"listener did not open");
 p.wire(unknown(p,100,0,2,6000));p.idle(300);
 check(p.exceptions.size()==1&&p.d.get_io$$state()==1&&p.packets.empty()&&p.tcpRoutes==0,
       "unrelated SYN consumed listener");
 refill(p);p.wire(p.encode(500,0,4,65535));p.idle(250);
 check(p.exceptions.size()==1&&p.d.get_io$$state()==1&&p.packets.empty(),"LISTEN incoming RST was not ignored");
 p.peerNext=40001;p.rx(40000,0,2);p.until([&]{return p.d.get_io$$sendActual()==p.planned;});
 p.rx(p.peerNext,p.planned);p.drain();check(p.d.get_io$$state()==4,"wildcard listener handshake failed");
 p.receiveData(std::vector<uint8_t>(33,0xa5));p.drain();
 p.write(81);p.until([&]{return p.dataPackets==1;});p.drain();p.ack(p.planned);p.drain();
 check(p.receivedBytes==33&&p.d.get_io$$sendOldest()==p.planned&&p.d.get_io$$freeBanks()==8&&p.invalid==0,
       "accepted listener connection failed data transfer");
 check(p.exceptions.size()==1,"accepted listener packet was routed to stateless responder");
 std::cout<<"SINGLE_LISTENER wildcard=pass wrong_port=pass rst_suppression=pass valid_handshake=pass data=pass cycles="<<p.cycles<<"\n";
}
int main(){try{closed();activeStall();listener();
 std::cout<<"SINGLE_CLOSED_RESPONSES_PASS unknown_tuple=pass txTcp_false=pass no_ledger=pass active=pass listener=pass rst_suppression=pass stalled_metadata=pass\n";return 0;
}catch(const std::exception&e){std::cerr<<"single_closed_responses: "<<e.what()<<'\n';return 1;}}
