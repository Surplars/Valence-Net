#include "TcpPmtuHarness.h"
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
struct Meta {uint32_t tag,gen,end;bool retry,data,consumes,timeoutRetry;uint32_t dst;uint64_t context;auto operator<=>(const Meta&)const=default;};
struct Packet {Meta meta;std::vector<uint8_t> bytes;};
struct Result {uint32_t tag,gen;bool failed=false;};
struct Block {uint32_t seq;std::vector<uint8_t> bytes;};
struct Test {
 STcpPmtuHarness d;std::mt19937_64 rng{0x10aabbcc};
 std::deque<Packet> pending;std::vector<Packet> packets;std::map<uint64_t,Block> blocks;
 using Beat=std::tuple<uint64_t,uint64_t,unsigned,bool,bool,Meta>;
 std::optional<Beat> held;std::optional<Meta> current;std::vector<uint8_t> bytes;
 std::optional<Result> injection;
 bool autoComplete=true,stallOutput=false,random=true,failNext=false;
 uint32_t planned=0,peerNext=0;unsigned mss=1460;
 unsigned cycles=0,completed=0,invalid=0,dataPackets=0,replayed=0,controls=0,stored=0,timeouts=0;
 unsigned concurrent=0,closes=0,opens=0;uint64_t context=0;
 Test(){
  d.set_io$$pathMtuLimit(1500);d.set_io$$pathUpdateValid(0);d.set_io$$pathUpdateGeneration(0);
  d.set_io$$pathUpdateMtu(1500);d.set_io$$pathUpdateIncrease(0);
  d.set_io$$rxSackPermitted(1);d.set_io$$sackCount(0);
  d.set_io$$sackLeft0(0);d.set_io$$sackLeft1(0);d.set_io$$sackLeft2(0);d.set_io$$sackLeft3(0);
  d.set_io$$sackRight0(0);d.set_io$$sackRight1(0);d.set_io$$sackRight2(0);d.set_io$$sackRight3(0);
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
  bool(d.get_io$$packetConsumes()),bool(d.get_io$$packetTimeoutRetry()),uint32_t(d.get_io$$packetDestination()),d.get_io$$packetContext()};}
 void validate(const Packet&p){
  auto&v=p.bytes;check(v.size()>=40&&v[0]==0x45&&u16(v,2)==v.size(),"bad IPv4 envelope");
  check(sum({v.begin(),v.begin()+20})==0&&v[9]==6&&v[8]==64,"bad IP checksum/TTL/protocol");
  check(u32(v,12)==0x0a000001&&u32(v,16)==0x0a000002&&p.meta.dst==0x0a000002,"wrong IP tuple");
  check(u16(v,20)==12345&&u16(v,22)==443,"wrong TCP ports");
  std::vector<uint8_t> pseudo(v.begin()+12,v.begin()+20);pseudo.push_back(0);pseudo.push_back(6);
  unsigned len=v.size()-20;pseudo.push_back(len>>8);pseudo.push_back(len);
  pseudo.insert(pseudo.end(),v.begin()+20,v.end());check(sum(pseudo)==0,"bad TCP checksum");
  unsigned hdr=(v[32]>>4)*4,n=v.size()-20-hdr;check(hdr>=20&&hdr<=60,"bad TCP offset");
  check(!p.meta.timeoutRetry||p.meta.retry,"timeout marker on original transmission");
  bool consume=n||(v[33]&3);check(p.meta.consumes==consume,"sequence-space descriptor wrong");
  check(p.meta.end==uint32_t(u32(v,24)+n+bool(v[33]&2)+bool(v[33]&1)),"wire end mismatch");
  if(p.meta.data){
   ++dataPackets;if(p.meta.retry)++replayed;
   auto it=blocks.find(p.meta.context);check(it!=blocks.end(),"unknown application context");
   auto&b=it->second;unsigned off=u32(v,24)-b.seq;
   check(n&&n<=mss&&off+n<=b.bytes.size(),"invalid segmentation");
   check(v[33]==(off+n==b.bytes.size()?24:16),"incorrect data flags");
   check(std::equal(v.begin()+20+hdr,v.end(),b.bytes.begin()+off),"payload changed / replay offset wrong");
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
 void path(unsigned mtu,bool increase=false,int generationDelta=0){
  d.set_io$$pathUpdateMtu(mtu);d.set_io$$pathUpdateIncrease(increase);
  d.set_io$$pathUpdateGeneration(uint32_t(d.get_io$$generation()+generationDelta));
  d.set_io$$pathUpdateValid(1);step();d.set_io$$pathUpdateValid(0);idle(3);
 }
 Packet nextReplay(unsigned before){
  until([&]{return replayed>before;});drain();
  for(auto i=packets.rbegin();i!=packets.rend();++i)if(i->meta.data&&i->meta.retry)return *i;
  throw std::runtime_error("replay absent");
 }
 void repair(uint32_t end,unsigned ceiling,unsigned before){
  uint32_t acked=d.get_io$$sendOldest();
  while(acked!=end){
   auto p=nextReplay(before++);
   check(u32(p.bytes,24)==acked,"PMTU repair skipped/duplicated sequence");
   check(p.bytes.size()<=ceiling,"PMTU repair exceeded path");
   check(!p.bytes.empty()&&p.meta.end!=acked,"empty PMTU repair");
   acked=p.meta.end;ack(acked);
  }
  idle(40);check(d.get_io$$freeBanks()==4,"PMTU repair leaked retained banks");
 }

 void close(bool abort=false){
  d.set_io$$abort(abort);d.set_io$$closeValid(1);until([&]{return d.get_io$$closeReady();});
  d.set_io$$closeValid(0);++closes;idle(12);
 }
};

int main(){try{
 Test t;t.random=false;
 t.active(0xfffffff0,1000);
 check(t.d.get_io$$pathMtu()==1500&&t.d.get_io$$sendMss()==1460,"initial path ceiling");
 uint32_t base=t.planned;t.write(2048);t.until([&]{return t.d.get_io$$sendActual()==t.planned;});t.drain();
 unsigned before=t.replayed,oldRto=t.d.get_io$$rto();auto reserved=t.d.get_io$$sendNext();
 t.path(600,false,1);t.path(67);t.path(1501,true);
 check(t.d.get_io$$pathMtu()==1500&&t.replayed==before,"invalid PMTU update affected state");
 t.path(600);
 check(t.d.get_io$$pathMtu()==600&&t.d.get_io$$sendMss()==560,"PMTU did not cap MSS");
 auto first=t.nextReplay(before);
 check(u32(first.bytes,24)==base&&first.bytes.size()==600,"initial PMTU repair incorrect");
 check(t.d.get_io$$sendNext()==reserved&&t.d.get_io$$freeBanks()==3,"PMTU replay changed ownership");
 check(t.d.get_io$$retries()==0&&t.d.get_io$$rto()==oldRto,"PMTU charged loss retry/backoff");
 t.path(600);t.path(600);t.path(1200);t.idle(120);
 check(t.replayed==before+1,"duplicate/nondecreasing PMTU report restarted repair");
 t.ack(first.meta.end);t.repair(t.planned,600,before+1);
 check(t.d.get_io$$retries()==0,"successful PMTU repair charged retries");

 // Increases need an explicit cache-expiry authorization and remain link bound.
 t.path(1200);check(t.d.get_io$$pathMtu()==600,"unsolicited PMTU increase accepted");
 t.path(1500,true);check(t.d.get_io$$sendMss()==1460,"expiry did not restore negotiated MSS");
 t.path(1600,true);check(t.d.get_io$$pathMtu()==1500,"expiry exceeded interface ceiling");

 // An already offered old-size packet must remain byte/descriptor stable.
 // Its reserved range becomes repairable only after real wire completion.
 base=t.planned;t.stallOutput=true;t.write(1460);
 t.until([&]{return t.d.get_io$$outValid();});before=t.replayed;
 t.path(900);t.path(296);t.idle(25);
 check(t.d.get_io$$sendActual()==base,"stalled packet falsely advanced actual");
 t.stallOutput=false;
 t.until([&]{return t.d.get_io$$sendActual()==t.planned;});
 t.repair(t.planned,296,before);
 check(t.d.get_io$$sendMss()==256&&t.d.get_io$$retries()==0,"repair MSS/retry state");


 // A second path reduction while an old repair is offered must restart
 // repair at the lower ceiling instead of waiting for the oversized retry.
 t.path(1500,true);t.write(1200);t.until([&]{return t.d.get_io$$sendActual()==t.planned;});t.drain();
 base=t.d.get_io$$sendOldest();t.stallOutput=true;before=t.replayed;t.path(900);
 t.until([&]{return t.d.get_io$$outValid();});
 auto oldRetry=t.meta();check(oldRetry.retry,"first PMTU retry not offered");
 t.path(296);t.idle(10);t.stallOutput=false;
 t.until([&]{return t.replayed>=before+2;});t.drain();
 check(t.packets[t.packets.size()-2].bytes.size()==900,"offered PMTU retry was rewritten");
 auto lower=t.packets.back();
 check(lower.meta.retry&&u32(lower.bytes,24)==base&&lower.bytes.size()==296,"second PMTU did not restart repair");
 t.ack(lower.meta.end);t.repair(t.planned,296,before+2);

 // The IPv4 minimum still permits 3 control SACK blocks. Data omits 4-block
 // options when their 36 bytes would leave no room below the 28-byte SMSS.
 t.path(68);t.idle(20);
 t.d.set_io$$sackCount(4);
 t.d.set_io$$sackLeft0(t.peerNext+64);t.d.set_io$$sackRight0(t.peerNext+96);
 t.d.set_io$$sackLeft1(t.peerNext+128);t.d.set_io$$sackRight1(t.peerNext+160);
 t.d.set_io$$sackLeft2(t.peerNext+192);t.d.set_io$$sackRight2(t.peerNext+224);
 t.d.set_io$$sackLeft3(t.peerNext+256);t.d.set_io$$sackRight3(t.peerNext+288);
 auto packetCount=t.packets.size();t.d.set_io$$updateWindow(1);t.step();t.d.set_io$$updateWindow(0);
 t.until([&]{return t.packets.size()>packetCount;});t.drain();
 auto sack=t.packets.back();
 check(!sack.meta.data&&sack.bytes.size()==68&&(sack.bytes[32]>>4)==12,"68-byte control SACK did not fit");
 check(sack.bytes[42]==5&&sack.bytes[43]==26,"control SACK block count wrong");
 packetCount=t.packets.size();t.write(128);t.until([&]{return t.d.get_io$$sendActual()==t.planned;});t.drain();
 for(size_t i=packetCount;i<t.packets.size();++i)if(t.packets[i].meta.data)
  check(t.packets[i].bytes.size()<=68&&(t.packets[i].bytes[32]>>4)==5,"tiny PMTU data exceeded ceiling");
 t.ack(t.planned);t.idle(40);check(t.d.get_io$$freeBanks()==4,"tiny PMTU leak");

 // Generation/reset fencing: an old path report cannot poison the next OPEN.
 unsigned oldGeneration=t.d.get_io$$generation();t.d.set_io$$sackCount(0);t.close(true);t.drain();
 t.active(50000,60000);
 check(t.d.get_io$$pathMtu()==1500,"reopened connection inherited PMTU");
 t.d.set_io$$pathUpdateGeneration(oldGeneration);t.d.set_io$$pathUpdateMtu(68);t.d.set_io$$pathUpdateValid(1);
 t.step();t.d.set_io$$pathUpdateValid(0);t.idle(10);
 check(t.d.get_io$$pathMtu()==1500&&t.d.get_io$$sendMss()==1460,"old-generation feedback applied");

 // PMTU repairs retain normal RTO recovery if the repair itself is lost.
 t.write(1024);t.until([&]{return t.d.get_io$$sendActual()==t.planned;});t.drain();
 before=t.replayed;t.path(600);t.nextReplay(before);
 check(t.d.get_io$$retries()==0,"PMTU repair consumed RTO budget");
 before=t.replayed;t.idle(t.d.get_io$$rto()+1,true);
 t.nextReplay(before);check(t.d.get_io$$retries()==1,"real RTO failed to consume retry budget");
 t.ack(t.planned);t.idle(60);check(t.d.get_io$$freeBanks()==4,"RTO PMTU ownership leak");
 t.close(true);t.drain();
 // A restored path ceiling never overrides the independently negotiated MSS.
 t.active(70000,80000,256);t.path(68);t.path(1500,true);
 check(t.d.get_io$$sendMss()==256,"PMTU increase overrode peer MSS");
 t.close(true);t.drain();
 // Initial interface MTU constrains the first data packet without changing
 // the receive MSS option advertised in SYN.
 t.d.set_io$$pathMtuLimit(296);t.active(90000,100000);
 check(t.d.get_io$$sendMss()==256&&t.d.get_io$$pathMtu()==296,"OPEN ignored path ceiling");
 packetCount=t.packets.size();t.write(1024);t.until([&]{return t.d.get_io$$sendActual()==t.planned;});t.drain();
 for(size_t i=packetCount;i<t.packets.size();++i)if(t.packets[i].meta.data)
  check(t.packets[i].bytes.size()<=296,"initial data exceeded interface MTU");
 t.ack(t.planned);t.close(true);t.drain();
 // FIN also carries SACK: preserve an offered 76-byte original, then
 // repair prior data and resend FIN with 3 blocks at PMTU68 without an RTO.
 t.d.set_io$$pathMtuLimit(1500);t.active(130000,140000);
 auto setSacks=[&]{
  t.d.set_io$$sackCount(4);
  t.d.set_io$$sackLeft0(t.peerNext+64);t.d.set_io$$sackRight0(t.peerNext+96);
  t.d.set_io$$sackLeft1(t.peerNext+128);t.d.set_io$$sackRight1(t.peerNext+160);
  t.d.set_io$$sackLeft2(t.peerNext+192);t.d.set_io$$sackRight2(t.peerNext+224);
  t.d.set_io$$sackLeft3(t.peerNext+256);t.d.set_io$$sackRight3(t.peerNext+288);
 };
 setSacks();t.write(128);t.until([&]{return t.d.get_io$$sendActual()==t.planned;});t.drain();
 t.stallOutput=true;t.close();t.until([&]{return t.d.get_io$$outValid();});
 check(t.meta().consumes&&!t.meta().data&&!t.meta().retry,"original FIN not offered");
 before=t.replayed;packetCount=t.packets.size();t.path(68);t.idle(40);
 check(t.d.get_io$$sendActual()==t.planned,"offered FIN falsely completed");
 t.stallOutput=false;t.until([&]{return t.d.get_io$$sendActual()==uint32_t(t.planned+1);});
 t.repair(t.planned,68,before);t.idle(40);t.drain();
 unsigned originalFin=0,repairFin=0;
 for(size_t i=packetCount;i<t.packets.size();++i){
  auto&p=t.packets[i];if(p.bytes[33]&1){
   if(p.meta.retry){++repairFin;check(p.bytes.size()==68&&!p.meta.timeoutRetry,"FIN PMTU repair size/origin");}
   else{++originalFin;check(p.bytes.size()==76,"offered FIN was mutated after PMTU change");}
   check(u32(p.bytes,24)==t.planned,"FIN path repair allocated new sequence");
  }
 }
 check(originalFin==1&&repairFin==1&&t.d.get_io$$retries()==0,"FIN repair required RTO or repeated");
 t.rx(t.peerNext,t.planned+1,17);++t.peerNext;t.drain();
 check(t.d.get_io$$state()==10,"PMTU FIN failed graceful simultaneous close");
 t.idle(25,true);t.idle(10);check(t.d.get_io$$state()==0,"PMTU FIN TIME-WAIT did not end");

 // CLOSE-WAIT still supports data, then the same PMTU repair in LAST-ACK.
 t.d.set_io$$sackCount(0);t.active(150000,160000);
 t.rx(t.peerNext,t.planned,17);++t.peerNext;
 check(t.d.get_io$$state()==7,"peer half-close lost");
 t.write(64);t.until([&]{return t.d.get_io$$sendActual()==t.planned;});t.ack(t.planned);t.drain();
 setSacks();t.close();t.until([&]{return t.d.get_io$$sendActual()==uint32_t(t.planned+1);});t.drain();
 check(t.d.get_io$$state()==9,"LAST-ACK missing");
 packetCount=t.packets.size();t.path(68);
 t.until([&]{return t.packets.size()>packetCount;});t.drain();
 auto lastFin=t.packets.back();
 check(lastFin.meta.retry&&!lastFin.meta.timeoutRetry&&(lastFin.bytes[33]&1)&&lastFin.bytes.size()==68,
  "LAST-ACK PMTU repair incorrect");
 t.rx(t.peerNext,t.planned+1);t.drain();
 check(t.d.get_io$$state()==0&&t.d.get_io$$retries()==0,"LAST-ACK completion failed");
 t.d.set_io$$sackCount(0);

 // Delayed PMTU completion can arrive after a subsequent RTO has queued.
 // Only the actual timeout-origin descriptor may consume that loss budget.
 Test q;q.random=false;q.active(110000,120000);
 q.write(1024);q.until([&]{return q.d.get_io$$sendActual()==q.planned;});q.drain();
 q.autoComplete=false;unsigned prior=q.replayed;q.path(600);
 q.until([&]{return q.replayed>prior;});
 check(q.pending.size()==1&&!q.pending.front().meta.timeoutRetry,"PMTU origin descriptor");
 auto earlier=q.pending.front();
 q.idle(q.d.get_io$$rto()+10,true);q.until([&]{return q.pending.size()>=2;});
 check(q.pending.back().meta.timeoutRetry&&q.d.get_io$$retries()==0,"RTO ownership before completion");
 q.injection=Result{earlier.meta.tag,earlier.meta.gen};q.step();q.idle(4);
 check(q.d.get_io$$retries()==0,"older PMTU completion consumed queued RTO budget");
 auto timeoutPacket=q.pending.front();
 q.injection=Result{timeoutPacket.meta.tag,timeoutPacket.meta.gen};q.step();q.idle(4);
 check(q.d.get_io$$retries()==1,"actual timeout completion not charged");
 q.ack(q.planned);q.autoComplete=true;q.close(true);q.drain();

 std::cout<<"TCP_PMTU_PASS cycles="<<t.cycles<<" data_packets="<<t.dataPackets<<" repairs="<<t.replayed
  <<" invalid_updates=pass wrap=pass ack_clock=pass reserved_horizon=pass stalled_descriptor=pass"
  <<" minimum_mtu_sack=pass checksums=pass karn_loss_budget=pass fin_pmtu_repair=pass half_close=pass delayed_timeout_origin=pass generation=pass reclamation=pass\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
