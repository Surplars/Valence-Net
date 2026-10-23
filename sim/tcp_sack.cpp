#include "TcpCongestedEndpointHarness.h"
#include "tcp_endpoint_peer.h"
using Peer=TcpEndpointPeer<STcpCongestedEndpointHarness>;
using Range=std::pair<uint32_t,uint32_t>;
static std::vector<uint8_t> options(std::vector<uint8_t> v,const std::vector<uint8_t>&opt) {
 unsigned old=(v[32]>>4)*4;
 check(opt.size()%4==0&&old+opt.size()<=60,"test option size");
 v.insert(v.begin()+20+old,opt.begin(),opt.end()); v[32]=((old+opt.size())/4)<<4;
 Peer::put16(v,2,v.size());Peer::put16(v,10,0);Peer::put16(v,36,0);
 std::vector<uint8_t> pseudo(v.begin()+12,v.begin()+20);pseudo.push_back(0);pseudo.push_back(6);
 unsigned n=v.size()-20;pseudo.push_back(n>>8);pseudo.push_back(n);pseudo.insert(pseudo.end(),v.begin()+20,v.end());
 Peer::put16(v,36,sum(pseudo));Peer::put16(v,10,sum({v.begin(),v.begin()+20}));return v;
}
static std::vector<uint8_t> sackOption(const std::vector<Range>&rs) {
 std::vector<uint8_t> out(4+rs.size()*8);out[0]=1;out[1]=1;out[2]=5;out[3]=2+rs.size()*8;
 for(unsigned i=0;i<rs.size();++i){Peer::put32(out,4+i*8,rs[i].first);Peer::put32(out,8+i*8,rs[i].second);}return out;
}
static std::vector<Range> sacks(const Packet&p) {
 const auto&v=p.bytes;unsigned end=20+(v[32]>>4)*4;std::vector<Range> rs;
 for(unsigned i=40;i<end;){unsigned kind=v[i];if(!kind)break;if(kind==1){++i;continue;}
  check(i+2<=end&&v[i+1]>=2&&i+v[i+1]<=end,"bad outgoing option");
  if(kind==5){check(v[i+1]>=10&&(v[i+1]-2)%8==0,"bad outgoing SACK length");
   for(unsigned j=i+2;j<i+v[i+1];j+=8)rs.emplace_back(u32(v,j),u32(v,j+4));}
  i+=v[i+1];
 }return rs;
}
static bool permitted(const Packet&p) {
 auto&v=p.bytes;for(unsigned i=40;i<20+(v[32]>>4)*4;){if(v[i]==0)break;if(v[i]==1){++i;continue;}
 if(v[i]==4&&v[i+1]==2)return true;i+=v[i+1];}return false;
}
static void active(Peer&p,uint32_t seq=1000,uint32_t peer=9000,unsigned mss=1460,bool permit=true) {
 p.open(seq,false,mss);p.until([&]{return p.d.get_io$$sendActual()==seq+1;});
 check(permitted(p.packets.front()),"active SYN did not advertise SACK permitted");
 p.peerNext=peer+1;auto syn=p.encode(peer,seq+1,18,65535);
 p.wire(permit?options(syn,{4,2,0,0}):syn);p.idle(250);check(p.d.get_io$$state()==4,"SACK handshake");p.drain();
}
static void ack(Peer&p,uint32_t number,const std::vector<Range>&rs,unsigned delay=120) {
 p.wire(options(p.encode(p.peerNext,number,16,65535),sackOption(rs)));p.idle(delay);
}
static Packet latestSack(const Peer&p,unsigned begin) {
 for(unsigned i=p.packets.size();i>begin;--i)if(!sacks(p.packets[i-1]).empty())return p.packets[i-1];
 throw std::runtime_error("no SACK ACK after durable reorder write");
}
static void receiveTests(bool wrap) {
 Peer p;p.random=true;active(p,1000,wrap?0xfffffff0:9000);uint32_t base=p.peerNext;
 std::vector<uint8_t> body(160);for(unsigned i=0;i<body.size();++i)body[i]=i*7+3;
 auto send=[&](unsigned off,unsigned len,unsigned flags=24){p.wire(p.encode(base+off,p.planned,flags,65535,
  {body.begin()+off,body.begin()+off+len}));p.idle(260);};
 unsigned begin=p.packets.size();send(32,16);
 check(sacks(latestSack(p,begin))==std::vector<Range>{{base+32,base+48}},"first durable block incorrect");
 begin=p.packets.size();send(64,16);
 for(unsigned i=begin;i<p.packets.size();++i){auto all=sacks(p.packets[i]);if(!all.empty())check(all[0]==Range{base+64,base+80},"early gap ACK reported older first block");}
 auto rs=sacks(latestSack(p,begin));check(rs.size()==2&&rs[0]==Range{base+64,base+80},"latest block first");
 begin=p.packets.size();send(48,16);
 rs=sacks(latestSack(p,begin));check(rs==std::vector<Range>{{base+32,base+80}},"adjacent ranges not merged");
 begin=p.packets.size();send(96,16);
 rs=sacks(latestSack(p,begin));check(rs.size()==2&&rs[0]==Range{base+96,base+112},"new isolated block first");
 begin=p.packets.size();for(unsigned repeat=0;repeat<12;++repeat)send(36,4);
 rs=sacks(latestSack(p,begin));check(rs.size()==2&&rs[0]==Range{base+32,base+80},"duplicate old block recency");
 begin=p.packets.size();send(128,16,25);
 rs=sacks(latestSack(p,begin));check(rs[0]==Range{base+128,base+144},"FIN entered SACK payload block");
 check(p.d.get_io$$receiveNext()==base,"OOO altered cumulative ACK");
 for(unsigned i=0;i<144;++i)p.expectedRx.push_back(body[i]);p.peerNext=base+145;
 send(0,32);send(80,16);send(112,16);p.idle(500);
 check(p.expectedRx.empty()&&p.d.get_io$$receiveNext()==base+145&&p.d.get_io$$state()==7,"SACK reassembly/FIN delivery");
 for(unsigned i=p.packets.size()-2;i<p.packets.size();++i)check(sacks(p.packets[i]).empty(),"stale block after cumulative advance");
}
static void fallback() {
 Peer p;active(p,100,400,64,false);uint32_t base=p.peerNext;unsigned begin=p.packets.size();
 p.wire(p.encode(base+16,p.planned,24,65535,std::vector<uint8_t>(16,7)));p.idle(400);
 for(unsigned i=begin;i<p.packets.size();++i)check(sacks(p.packets[i]).empty(),"SACK sent without negotiation");
 // Unnegotiated peer SACK cannot suppress ordinary Reno replay.
 p.write(64);p.until([&]{return p.d.get_io$$sendActual()==p.planned;});unsigned replay=p.replayed;
 uint32_t oldest=p.d.get_io$$sendOldest();for(unsigned i=0;i<3;++i)ack(p,oldest,{{oldest+1,oldest+64}});
 p.until([&]{return p.replayed>replay;});auto&v=p.packets.back().bytes;
 check(u32(v,24)==oldest&&v.size()-20-(v[32]>>4)*4==64,"unnegotiated SACK used");
}
static void recovery(bool wrap) {
 Peer p;active(p,wrap?0xfffffd80:1000,9000,64);
 // Grow Reno's window before introducing a six-segment flight.
 for(unsigned i=0;i<8;++i){p.write(64);p.until([&]{return p.d.get_io$$sendActual()==p.planned;});p.ack(p.planned);}
 uint32_t base=p.planned;unsigned start=p.packets.size();p.write(384);
 p.until([&]{return p.d.get_io$$sendActual()==p.planned;});p.drain();
 unsigned free=p.d.get_io$$freeBanks(),before=p.replayed;
 std::vector<Range> rs{{base+64,base+128},{base+192,base+256},{base+320,base+384}};
 for(unsigned i=0;i<5;++i)ack(p,base,rs);
 p.until([&]{return p.replayed>=before+3;});p.drain();
 std::vector<Range> replay;
 for(unsigned i=start;i<p.packets.size();++i)if(p.packets[i].meta.retry&&p.packets[i].meta.data){
  auto&v=p.packets[i].bytes;replay.emplace_back(u32(v,24),p.packets[i].meta.end);
 }
 check(replay==std::vector<Range>{{base,base+64},{base+128,base+192},{base+256,base+320}},"multiple-loss replay included SACKed bytes");
 check(p.d.get_io$$freeBanks()==free&&p.d.get_io$$sendOldest()==base,"SACK released retained payload");
 // Partial cumulative ACK lets oldest enter a formerly SACKed range. Receiver
 // reneging must still recover it on RTO from the original retained bytes.
 p.ack(base+64);before=p.replayed;
 for(unsigned i=0;i<1000&&p.replayed==before;++i)p.idle(1,true);
 p.until([&]{return p.replayed>before;});p.drain();
 const Packet*last=nullptr;for(auto&packet:p.packets)if(packet.meta.retry&&packet.meta.data)last=&packet;
 check(last&&u32(last->bytes,24)==base+64,"RTO trusted stale SACK after reneging");
 p.ack(p.planned);p.idle(200);check(p.d.get_io$$freeBanks()==4,"cumulative ACK did not release data");
 // New generation does not inherit blocks or the negotiated capability.
 p.close(true);p.drain();p.blocks.clear();active(p,30000,45000,64,false);
 before=p.packets.size();p.wire(p.encode(p.peerNext+8,p.planned,24,65535,std::vector<uint8_t>(8,3)));p.idle(400);
 for(unsigned i=before;i<p.packets.size();++i)check(sacks(p.packets[i]).empty(),"new generation inherited SACK state");
}
static void malicious() {
 Peer p;active(p,1000,9000,64);
 p.write(64);p.until([&]{return p.d.get_io$$sendActual()==p.planned;});
 uint32_t base=p.d.get_io$$sendOldest();unsigned before=p.replayed;
 std::vector<Range> rs{{base+1,base+65},{base+10,base+10},{base+63,base+10},{base-1,base+20}};
 for(unsigned i=0;i<3;++i)ack(p,base,rs);
 p.until([&]{return p.replayed>before;});p.drain();
 auto&v=p.packets.back().bytes;check(u32(v,24)==base&&p.packets.back().meta.end==base+64,"malicious SACK changed replay");
}
static void mtu() {
 Peer p;active(p);uint32_t base=p.peerNext;
 for(unsigned off:{16,48,80,112}){p.wire(p.encode(base+off,p.planned,24,65535,std::vector<uint8_t>(8,7)));p.idle(260);}
 auto rs=sacks(latestSack(p,0));check(rs.size()==4,"four-block encoding");
 unsigned n=p.packets.size();p.write(1460);p.idle(1000);
 for(unsigned i=n;i<p.packets.size();++i)if(p.packets[i].meta.data){
  check(p.packets[i].bytes.size()<=1500,"SACK options exceeded IP MTU");check(sacks(p.packets[i]).size()==4,"data ACK omitted SACK");
 }
 p.ack(p.d.get_io$$sendActual());p.until([&]{return p.d.get_io$$sendActual()==p.planned;});p.drain();
}
static void unaligned() {
 Peer p;active(p,0xffffffc0,9000,64);
 p.write(64);p.until([&]{return p.d.get_io$$sendActual()==p.planned;});p.ack(p.planned);
 uint32_t base=p.planned;unsigned begin=p.packets.size();p.write(128);
 p.until([&]{return p.d.get_io$$sendActual()==p.planned;});unsigned before=p.replayed;
 std::vector<Range> rs{{base+17,base+39},{base+70,base+128}};
 for(unsigned i=0;i<4;++i)ack(p,base,rs);
 p.until([&]{return p.replayed>=before+2;});p.drain();
 std::vector<Range> got;for(unsigned i=begin;i<p.packets.size();++i)if(p.packets[i].meta.retry&&p.packets[i].meta.data)
  got.emplace_back(u32(p.packets[i].bytes,24),p.packets[i].meta.end);
 check(got==std::vector<Range>{{base,base+17},{base+39,base+70}},"unaligned SACK hole boundary lost");
}
static void dynamicMtu() {
 Peer p;active(p);
 for(unsigned i=0;i<4;++i){p.write(1460);p.until([&]{return p.d.get_io$$sendActual()==p.planned;});p.ack(p.planned);}
 unsigned begin=p.packets.size();p.stallOutput=true;
 for(unsigned i=0;i<4;++i)p.write(1460);
 p.wire(p.encode(p.peerNext+16,p.d.get_io$$sendOldest(),24,65535,std::vector<uint8_t>(8,7)));p.idle(350);
 p.stallOutput=false;p.until([&]{return p.d.get_io$$sendActual()==p.planned;});p.drain();
 for(unsigned i=begin;i<p.packets.size();++i)if(p.packets[i].meta.data)
  check(p.packets[i].bytes.size()<=1500,"header grew after MSS reservation during backpressure");
}
static void passive() {
 Peer p;p.open(5000,true,256);p.peerNext=9001;
 p.wire(options(p.encode(9000,0,2,65535),{4,2,0,0}));p.idle(300);
 check(p.d.get_io$$state()==3&&permitted(p.packets.back()),"passive SACK negotiation");
 p.rx(p.peerNext,p.planned);check(p.d.get_io$$state()==4,"passive final ACK");
 unsigned start=p.packets.size();p.wire(p.encode(p.peerNext+16,p.planned,24,65535,std::vector<uint8_t>(8,7)));p.idle(300);
 check(!sacks(latestSack(p,start)).empty(),"passive session cannot generate SACK");
}

static void burst() {
 Peer p;p.random=false;active(p);uint32_t base=p.peerNext;unsigned begin=p.packets.size();
 for(unsigned i=0;i<8;++i)p.wire(p.encode(base+16+i*16,p.planned,24,65535,std::vector<uint8_t>(8,i)));
 p.idle(1800);unsigned published=0;uint32_t newest=base;
 for(unsigned i=begin;i<p.packets.size();++i){auto rs=sacks(p.packets[i]);if(!rs.empty()){
  ++published;check(uint32_t(rs[0].first-base)>=uint32_t(newest-base),"burst SACK recency regressed");
  newest=rs[0].first;check(rs.size()<=4,"option overflow in burst");
 }}
 check(p.d.get_io$$reorderCount()==8&&published>=8&&newest==base+128,"continuous OOO burst starved SACK publication");
}

static void tinyMss(unsigned mss,unsigned gaps) {
 Peer p;active(p,1000,9000,mss);uint32_t base=p.peerNext;
 for(unsigned i=0;i<gaps;++i){p.wire(p.encode(base+16+i*16,p.planned,24,65535,std::vector<uint8_t>(1,i)));p.idle(260);}
 auto control=sacks(latestSack(p,0));check(control.size()==gaps,"tiny-MSS control ACK lost SACK report");
 unsigned begin=p.packets.size();p.write(mss);p.until([&]{return p.d.get_io$$sendActual()==p.planned;});p.drain();
 unsigned total=0;bool useOptions=mss>4+gaps*8;
 for(unsigned i=begin;i<p.packets.size();++i)if(p.packets[i].meta.data){
  const auto&packet=p.packets[i];const auto&v=packet.bytes;unsigned header=(v[32]>>4)*4;
  check(v.size()<=40+mss,"tiny-MSS packet exceeded fixed-header plus MSS budget");
  check(sacks(packet).size()==(useOptions?gaps:0),"tiny-MSS option fallback wrong");
  total+=v.size()-20-header;
 }
 check(total==mss,"tiny-MSS data stopped making progress");
}

static void stableSmss() {
 Peer p;p.random=false;active(p,1000,9000,64);
 // Receive-side gaps add options to our outgoing data. That packet's smaller
 // payload budget must not change the connection's negotiated Reno SMSS.
 unsigned begin=p.packets.size();
 p.wire(p.encode(p.peerNext+16,p.planned,24,65535,std::vector<uint8_t>(7,9)));
 p.idle(300);check(sacks(latestSack(p,begin)).size()==1,"SMSS test did not publish receive SACK");
 p.write(64);p.until([&]{return p.d.get_io$$sendActual()==p.planned;});p.drain();
 unsigned before=p.replayed;
 for(unsigned i=0;i<1000&&p.replayed==before;++i)p.idle(1,true);
 p.until([&]{return p.replayed>before;});p.idle(50);
 check(p.d.get_io$$congestionLimit()==64,"RTO window used live SACK option budget instead of negotiated SMSS");
 p.ack(p.planned);p.drain();
}
int main(){try{receiveTests(false);receiveTests(true);fallback();recovery(false);recovery(true);malicious();unaligned();mtu();dynamicMtu();passive();burst();tinyMss(16,1);tinyMss(16,4);tinyMss(1,1);stableSmss();
 std::cout<<"PASS tcp_sack: negotiation/fallback, durable merged latest-first blocks, duplicate recency, FIN, wrap, multiple holes, malicious blocks, retention/reneging/RTO, generations and MTU\n";return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL tcp_sack: "<<e.what()<<'\n';return 1;}}
