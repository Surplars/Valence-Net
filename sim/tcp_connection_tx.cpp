#include "TcpConnectionTxHarness.h"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
#include <tuple>
#include <vector>
static void check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
static unsigned u16(const std::vector<uint8_t>&v,unsigned p){return (unsigned(v.at(p))<<8)|v.at(p+1);}
static uint32_t u32(const std::vector<uint8_t>&v,unsigned p){return (uint32_t(u16(v,p))<<16)|u16(v,p+2);}
static uint16_t sum(std::vector<uint8_t> v){if(v.size()%2)v.push_back(0);uint32_t n=0;
 for(unsigned i=0;i<v.size();i+=2)n+=u16(v,i);while(n>>16)n=(n&65535)+(n>>16);return uint16_t(~n);}
struct Meta {uint32_t tag,gen,end;bool retry,data,consumes;uint32_t dst;uint64_t context;auto operator<=>(const Meta&)const=default;};
struct Packet {Meta meta;std::vector<uint8_t> bytes;};
struct Result {uint32_t tag,gen;bool failed=false;};
struct Block {uint32_t seq;std::vector<uint8_t> bytes;};
struct Test {
 STcpConnectionTxHarness d;std::mt19937_64 rng{0x10aabbcc};
 std::deque<Packet> pending;std::vector<Packet> packets;std::map<uint64_t,Block> blocks;
 using Beat=std::tuple<uint64_t,uint64_t,unsigned,bool,bool,Meta>;
 std::optional<Beat> held;std::optional<Meta> current;std::vector<uint8_t> bytes;
 std::optional<Result> injection;
 bool autoComplete=true,stallOutput=false,random=true,failNext=false;
 uint32_t planned=0,peerNext=0;unsigned mss=1460;
 unsigned cycles=0,completed=0,invalid=0,dataPackets=0,replayed=0,controls=0,stored=0,timeouts=0;
 unsigned concurrent=0,closes=0,opens=0;uint64_t context=0;
 Test(){
  d.set_io$$openValid(0);d.set_io$$closeValid(0);d.set_io$$rxValid(0);d.set_io$$allocateValid(0);d.set_io$$valid(0);
  d.set_io$$completedValid(0);d.set_io$$outReady(0);d.set_io$$storedReady(1);
  d.set_io$$updateWindow(0);
  d.set_io$$receiveWindow(16384);d.set_io$$congestionWindow(65535);
  d.set_io$$dispositionReady(1);d.set_io$$ackReady(1);d.set_io$$noticeReady(1);d.set_io$$tick(0);
  d.set_io$$localIp(0x0a000001);d.set_io$$remoteIp(0x0a000002);
  d.set_io$$localPort(12345);d.set_io$$remotePort(443);d.set_io$$offerScale(1);d.set_io$$localScale(0);
  d.set_io$$rxSourceIp(0x0a000002);d.set_io$$rxDestinationIp(0x0a000001);
  d.set_io$$rxSourcePort(443);d.set_io$$rxDestinationPort(12345);
  d.set_io$$rxLength(0);d.set_io$$rxErrors(0);d.set_io$$rxUrgent(0);
  d.set_io$$payloadStored(1);d.set_io$$payloadToken(0);
  d.set_io$$rxScalePresent(0);d.set_io$$rxScale(0);d.set_io$$rxMssPresent(0);
  d.set_reset(1);d.step();d.step();d.set_reset(0);idle(5);
 }
 Meta meta(){return {uint32_t(d.get_io$$packetTag()),uint32_t(d.get_io$$packetGeneration()),
  uint32_t(d.get_io$$packetEnd()),bool(d.get_io$$packetRetry()),bool(d.get_io$$packetData()),
  bool(d.get_io$$packetConsumes()),uint32_t(d.get_io$$packetDestination()),d.get_io$$packetContext()};}
 void validate(const Packet&p){
  auto&v=p.bytes;check(v.size()>=40&&v[0]==0x45&&u16(v,2)==v.size(),"bad IPv4 envelope");
  check(sum({v.begin(),v.begin()+20})==0&&v[9]==6&&v[8]==64,"bad IP checksum/TTL/protocol");
  check(u32(v,12)==0x0a000001&&u32(v,16)==0x0a000002&&p.meta.dst==0x0a000002,"wrong IP tuple");
  check(u16(v,20)==12345&&u16(v,22)==443,"wrong TCP ports");
  std::vector<uint8_t> pseudo(v.begin()+12,v.begin()+20);pseudo.push_back(0);pseudo.push_back(6);
  unsigned len=v.size()-20;pseudo.push_back(len>>8);pseudo.push_back(len);
  pseudo.insert(pseudo.end(),v.begin()+20,v.end());check(sum(pseudo)==0,"bad TCP checksum");
  unsigned hdr=(v[32]>>4)*4,n=v.size()-20-hdr;check(hdr>=20&&hdr<=60,"bad TCP offset");
  bool consume=n||(v[33]&3);check(p.meta.consumes==consume,"sequence-space descriptor wrong");
  check(p.meta.end==uint32_t(u32(v,24)+n+bool(v[33]&2)+bool(v[33]&1)),"wire end mismatch");
  if(p.meta.data){
   ++dataPackets;if(p.meta.retry)++replayed;
   auto it=blocks.find(p.meta.context);check(it!=blocks.end(),"unknown application context");
   auto&b=it->second;unsigned off=u32(v,24)-b.seq;
   check(n&&n<=mss&&off+n<=b.bytes.size()&&hdr==20,"invalid segmentation");
   check(v[33]==(off+n==b.bytes.size()?24:16),"incorrect data flags");
   check(std::equal(v.begin()+40,v.end(),b.bytes.begin()+off),"payload changed / replay offset wrong");
   check(u32(v,28)==peerNext,"data ACK not from receive state");
  }else{
   ++controls;check(n==0,"control carries unexpected payload");
   if(v[33]&2){check(u32(v,24)==planned-1&&v[40]==2&&v[41]==4&&u16(v,42)==mss,"SYN options/ISN wrong");}
  }
 }
 void step(bool tick=false){
  bool ready=!stallOutput&&(!random||rng()%4);
  d.set_io$$outReady(ready);d.set_io$$tick(tick);
  d.set_io$$ackReady(!random||rng()%4);d.set_io$$noticeReady(!random||rng()%4);d.set_io$$dispositionReady(!random||rng()%4);
  std::optional<Result> r=injection;
  if(!r&&autoComplete&&!pending.empty()&&(!random||rng()%3))r=Result{pending.front().meta.tag,pending.front().meta.gen,failNext};
  bool injected=bool(injection);injection.reset();
  d.set_io$$completedValid(bool(r));
  if(r){d.set_io$$completedTag(r->tag);d.set_io$$completedGeneration(r->gen);d.set_io$$completedFailed(r->failed);}
  d.step();++cycles;
  if(d.get_io$$fault()){std::cerr<<"fault cycle="<<cycles<<" state="<<d.get_io$$state()<<'\n';throw std::runtime_error("TX integration fault");}
  if(r){
   if(d.get_io$$completionAccepted()){
    check(!pending.empty()&&pending.front().meta.tag==r->tag&&pending.front().meta.gen==r->gen,"accepted wrong completion");
    pending.pop_front();++completed;if(!injected)failNext=false;
   }else{check(d.get_io$$invalidCompletion(),"completion neither accepted nor rejected");++invalid;}
  }
  Beat beat={d.get_io$$outLo(),d.get_io$$outHi(),unsigned(d.get_io$$outKeep()),
    bool(d.get_io$$outLast()),bool(d.get_io$$outError()),meta()};
  if(held)check(d.get_io$$outValid()&&beat==*held,"offered packet changed while stalled");
  held=d.get_io$$outValid()&&!ready?std::optional<Beat>(beat):std::nullopt;
  if(d.get_io$$outValid()&&ready){
   check(!d.get_io$$outError(),"error in serialized packet");
   if(!current)current=meta();check(meta()==*current,"packet descriptor changed mid-frame");
   unsigned keep=d.get_io$$outKeep(),n=__builtin_popcount(keep);
   check(keep&&(keep&(keep+1))==0,"invalid keep");
   for(unsigned i=0;i<n;++i)bytes.push_back((i<8?d.get_io$$outLo():d.get_io$$outHi())>>(8*(i%8)));
   if(d.get_io$$outLast()){
    Packet p{*current,bytes};validate(p);pending.push_back(p);packets.push_back(p);bytes.clear();current.reset();
   }
  }
  if(d.get_io$$storedValid()){check(!d.get_io$$storedError(),"unexpected store error");++stored;}
  if(d.get_io$$timeoutValid())++timeouts;

 }
 void idle(unsigned n,bool tick=false){while(n--)step(tick);}
 template<class F>void until(F f,unsigned limit=20000){for(unsigned i=0;i<limit;++i){step();if(f())return;}throw std::runtime_error("integration wait timed out");}
 void drain(){until([&]{return pending.empty()&&!d.get_io$$outstanding()&&!d.get_io$$outValid();});idle(30);}
 void open(uint32_t seq,bool passive=false,unsigned newMss=1460){
  mss=newMss;planned=seq+1;peerNext=0;
  d.set_io$$initialSequence(seq);d.set_io$$passiveInitialSequence(seq);d.set_io$$localMss(mss);
  d.set_io$$passive(passive);d.set_io$$openValid(1);until([&]{return d.get_io$$openReady();});
  d.set_io$$openValid(0);++opens;idle(5);
 }
 void rx(uint32_t seq,uint32_t ack,unsigned flags=16,unsigned window=65535){
  d.set_io$$rxSequence(seq);d.set_io$$rxAcknowledgement(ack);d.set_io$$rxFlags(flags);d.set_io$$rxWindow(window);
  d.set_io$$rxMssPresent(bool(flags&2));d.set_io$$rxMss(mss);
  d.set_io$$rxValid(1);until([&]{return d.get_io$$rxReady();});d.set_io$$rxValid(0);idle(12);
 }
 void active(uint32_t seq,uint32_t peer,unsigned smss=1460){
  open(seq,false,smss);until([&]{return d.get_io$$sendActual()==seq+1;});
  peerNext=peer+1;rx(peer,seq+1,18);check(d.get_io$$state()==4,"active handshake failed");drain();
 }
 void write(unsigned n){
  Block b{planned,std::vector<uint8_t>(n)};for(auto&v:b.bytes)v=rng();uint64_t id=++context;blocks.emplace(id,b);planned+=n;
  unsigned before=stored;d.set_io$$length(n);d.set_io$$context(id);d.set_io$$allocateValid(1);
  until([&]{return d.get_io$$allocateReady();});d.set_io$$allocateValid(0);
  for(unsigned off=0;off<n||off==0;off+=16){
   unsigned count=std::min(16U,n-off);uint64_t lo=0,hi=0;
   for(unsigned i=0;i<count;++i)(i<8?lo:hi)|=uint64_t(b.bytes[off+i])<<(8*(i%8));
   d.set_io$$lo(lo);d.set_io$$hi(hi);d.set_io$$keep((1U<<count)-1);d.set_io$$last(off+count==n);d.set_io$$error(0);d.set_io$$valid(1);
   until([&]{return d.get_io$$ready();});d.set_io$$valid(0);if(!n)break;
  }
  until([&]{return stored>before;});
 }
 void ack(uint32_t seq){rx(peerNext,seq);}
 void close(bool abort=false){
  d.set_io$$abort(abort);d.set_io$$closeValid(1);until([&]{return d.get_io$$closeReady();});
  d.set_io$$closeValid(0);++closes;idle(12);
 }
};
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
 std::cout<<"TCP_CONNECTION_TX_PASS cycles="<<t.cycles<<" opens="<<t.opens<<" writes="<<t.stored
  <<" data_packets="<<t.dataPackets<<" retransmissions="<<t.replayed<<" controls="<<t.controls
  <<" completions="<<t.completed<<" rejected_completions="<<t.invalid
  <<" wire_checksums=pass partial_ack=pass ledger_backpressure=pass abort_stability=pass route_failure=pass reuse=pass\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
