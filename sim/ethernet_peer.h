#pragma once
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
struct Packet {Meta meta;std::vector<uint8_t> bytes;bool failed=false;};
struct Result {uint32_t tag,gen;bool failed=false;};
struct Block {uint32_t seq;std::vector<uint8_t> bytes;};
template<class Harness> struct EthernetPeer {
 Harness d;std::mt19937_64 rng{0x10aabbcc};
 std::deque<Packet> pending;std::vector<Packet> packets;std::map<uint64_t,Block> blocks;
 using Beat=std::tuple<uint64_t,uint64_t,unsigned,bool,bool,Meta>;
 std::optional<Beat> held;std::optional<Meta> current;std::vector<uint8_t> bytes;
 std::optional<Result> injection;
 bool autoComplete=true,stallOutput=false,random=true,failNext=false,stallReceive=false;
 std::deque<std::vector<uint8_t>> ingress;unsigned ingressOffset=0,ingressFrames=0,arpRequests=0;bool answerArp=true;
 std::deque<std::vector<uint8_t>> udpTxExpected,udpRxExpected,echoExpected;
 unsigned udpRxOffset=0,udpTxPackets=0,udpRxPackets=0,echoPackets=0;
 bool mixed=false,autoPeerAck=false;std::vector<uint32_t> allowedAck;
 std::vector<unsigned> dataEndTimes,udpTxEndTimes,udpRxEndTimes,tcpRxByteTimes;
 std::deque<uint8_t> expectedRx;unsigned receivedBytes=0,drops=0;
 uint32_t planned=0,peerNext=0;unsigned mss=1460;
 unsigned cycles=0,completed=0,invalid=0,dataPackets=0,replayed=0,controls=0,stored=0,timeouts=0;
 unsigned concurrent=0,closes=0,opens=0;uint64_t context=0;
 EthernetPeer(){
  d.set_io$$udpSendValid(0);d.set_io$$udpValid(0);d.set_io$$udpReceivedReady(0);
  d.set_io$$flushArp(0);
  d.set_io$$netValid(0);d.set_io$$netError(0);d.set_io$$receivedReady(0);
  d.set_io$$openValid(0);d.set_io$$closeValid(0);d.set_io$$allocateValid(0);d.set_io$$valid(0);
  d.set_io$$completedValid(0);d.set_io$$outReady(0);d.set_io$$storedReady(1);
  
  d.set_io$$congestionWindow(65535);
  d.set_io$$noticeReady(1);d.set_io$$tick(0);
  d.set_io$$localIp(0x0a000001);d.set_io$$remoteIp(0x0a000002);
  d.set_io$$localPort(12345);d.set_io$$remotePort(443);d.set_io$$offerScale(1);d.set_io$$localScale(0);
  
  
  
  
  
  d.set_reset(1);d.step();d.step();d.set_reset(0);idle(5);
 }
 Meta meta(){if(!d.get_io$$outTcp())return {};return {uint32_t(d.get_io$$packetTag()),uint32_t(d.get_io$$packetGeneration()),
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
   check(u32(v,28)==peerNext||(mixed&&std::find(allowedAck.begin(),allowedAck.end(),u32(v,28))!=allowedAck.end()),"data ACK not from receive state");
  }else{
   ++controls;check(n==0,"control carries unexpected payload");
   if(v[33]&2){check(u32(v,24)==planned-1&&v[40]==2&&v[41]==4&&u16(v,42)==mss,"SYN options/ISN wrong");}
  }
 }
 void step(bool tick=false){
  bool ready=!stallOutput&&(!random||rng()%4);
  d.set_io$$outReady(ready);d.set_io$$tick(tick);
  d.set_io$$noticeReady(!random||rng()%4);
  std::optional<Result> r=injection;
  if(!r&&autoComplete&&!pending.empty()&&(!random||rng()%3))r=Result{pending.front().meta.tag,pending.front().meta.gen,failNext||pending.front().failed};
  bool injected=bool(injection);injection.reset();
  d.set_io$$completedValid(bool(r));
  if(r){d.set_io$$completedTag(r->tag);d.set_io$$completedGeneration(r->gen);d.set_io$$completedFailed(r->failed);}
  bool udpReady=!random||rng()%4;d.set_io$$udpReceivedReady(udpReady);
  bool readReady=!stallReceive&&(!random||rng()%4);d.set_io$$receivedReady(readReady);
  bool netDriving=!ingress.empty();unsigned netCount=0;
  if(netDriving){
   auto&v=ingress.front();netCount=std::min(16U,unsigned(v.size())-ingressOffset);uint64_t lo=0,hi=0;
   for(unsigned i=0;i<netCount;++i)(i<8?lo:hi)|=uint64_t(v[ingressOffset+i])<<(8*(i%8));
   d.set_io$$netLo(lo);d.set_io$$netHi(hi);d.set_io$$netKeep((1U<<netCount)-1);
   d.set_io$$netLast(ingressOffset+netCount==v.size());
  }
  d.set_io$$netValid(netDriving);d.step();++cycles;
  if(netDriving&&d.get_io$$netReady()){
   ingressOffset+=netCount;
   if(ingressOffset==ingress.front().size()){ingress.pop_front();ingressOffset=0;++ingressFrames;}
  }
  if(d.get_io$$fault()){std::cerr<<"fault cycle="<<cycles<<" state="<<d.get_io$$state()<<'\n';throw std::runtime_error("TX integration fault");}
  if(r){
   if(d.get_io$$completionAccepted()){
    check(!pending.empty()&&pending.front().meta.tag==r->tag&&pending.front().meta.gen==r->gen,"accepted wrong completion");
    if(autoPeerAck&&pending.front().meta.data&&!pending.front().failed){
     ingress.push_back(encode(peerNext,pending.front().meta.end,16,65535));
    }
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
    check(bytes.size()>=60,"Ethernet minimum padding");
    check((u32(bytes,0)==0x02000000&&u16(bytes,4)==2)||(u32(bytes,0)==0xffffffff&&u16(bytes,4)==0xffff),"Ethernet destination");
    unsigned type=u16(bytes,12);
    if(type==0x0806){
     check(u16(bytes,20)==1&&u32(bytes,38)==0x0a000002,"unexpected ARP request");++arpRequests;
     if(answerArp){
      std::vector<uint8_t> reply(60);reply[0]=2;reply[5]=1;reply[6]=2;reply[11]=2;put16(reply,12,0x0806);
      put16(reply,14,1);put16(reply,16,0x0800);reply[18]=6;reply[19]=4;put16(reply,20,2);
      reply[22]=2;reply[27]=2;put32(reply,28,0x0a000002);reply[32]=2;reply[37]=1;put32(reply,38,0x0a000001);
      ingress.push_back(reply);
     }
    }else{
     check(type==0x0800,"unexpected frame source");
     unsigned total=u16(bytes,16);check(14+total<=bytes.size(),"Ethernet IP truncation");
     std::vector<uint8_t> ip(bytes.begin()+14,bytes.begin()+14+total);
     if(ip[9]==6){
      check(d.get_io$$outTcp(),"TCP frame lost completion sideband");
      Packet p{*current,ip};validate(p);pending.push_back(p);packets.push_back(p);if(p.meta.data)dataEndTimes.push_back(cycles);
     }else{
      check(!d.get_io$$outTcp()&&sum({ip.begin(),ip.begin()+20})==0,"non-TCP metadata / IP checksum");
      check(u32(ip,12)==0x0a000001&&u32(ip,16)==0x0a000002,"non-TCP IP tuple");
      if(ip[9]==17){
       check(!udpTxExpected.empty()&&u16(ip,20)==10000&&u16(ip,22)==20000&&u16(ip,24)==ip.size()-20,"UDP envelope");
       std::vector<uint8_t> pseudo(ip.begin()+12,ip.begin()+20);pseudo.push_back(0);pseudo.push_back(17);
       unsigned n=ip.size()-20;pseudo.push_back(n>>8);pseudo.push_back(n);pseudo.insert(pseudo.end(),ip.begin()+20,ip.end());
       check(u16(ip,26)&&sum(pseudo)==0,"UDP transmit checksum");
       check(std::vector<uint8_t>(ip.begin()+28,ip.end())==udpTxExpected.front(),"UDP transmit payload");
       udpTxExpected.pop_front();++udpTxPackets;udpTxEndTimes.push_back(cycles);
      }else{
       check(ip[9]==1&&!echoExpected.empty(),"unexpected protocol");
       check(std::vector<uint8_t>(ip.begin()+20,ip.end())==echoExpected.front()&&sum({ip.begin()+20,ip.end()})==0,"ICMP echo mismatch");
       echoExpected.pop_front();++echoPackets;
      }
     }
    }
    bytes.clear();current.reset();
   }
  }
  if(d.get_io$$storedValid()){check(!d.get_io$$storedError(),"unexpected store error");++stored;}
  if(d.get_io$$routeValid()&&d.get_io$$routeFailed()){
   Meta m{};m.tag=d.get_io$$routeTag();m.gen=d.get_io$$routeGeneration();
   pending.push_back(Packet{m,{},true});
  }
  if(d.get_io$$udpReceivedValid()&&udpReady){
   check(!udpRxExpected.empty(),"unexpected UDP receive");
   auto&v=udpRxExpected.front();unsigned n=__builtin_popcount(d.get_io$$udpReceivedKeep());
   check(d.get_io$$udpReceivedLength()==v.size()&&d.get_io$$udpReceivedSource()==0x0a000002&&d.get_io$$udpReceivedPort()==20000,"UDP receive metadata");
   check(udpRxOffset+n<=v.size(),"UDP receive overflow");
   for(unsigned i=0;i<n;++i){uint8_t b=(i<8?d.get_io$$udpReceivedLo():d.get_io$$udpReceivedHi())>>(8*(i%8));
    check(b==v[udpRxOffset++],"UDP receive payload");}
   check(bool(d.get_io$$udpReceivedLast())==(udpRxOffset==v.size()),"UDP boundary");
   if(d.get_io$$udpReceivedLast()){udpRxExpected.pop_front();udpRxOffset=0;++udpRxPackets;udpRxEndTimes.push_back(cycles);}
  }
  if(d.get_io$$timeoutValid())++timeouts;
  drops+=d.get_io$$rxDrops();
  if(d.get_io$$receivedValid()&&readReady){
   unsigned n=__builtin_popcount(d.get_io$$receivedKeep());check(n&&expectedRx.size()>=n,"unexpected received bytes");
   for(unsigned i=0;i<n;++i){uint8_t b=(i<8?d.get_io$$receivedLo():d.get_io$$receivedHi())>>(8*(i%8));
    check(b==expectedRx.front(),"receive byte stream corrupted");expectedRx.pop_front();++receivedBytes;tcpRxByteTimes.push_back(cycles);}
  }

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
 static void put16(std::vector<uint8_t>&v,unsigned off,unsigned n){v[off]=n>>8;v[off+1]=n;}
 static void put32(std::vector<uint8_t>&v,unsigned off,uint32_t n){put16(v,off,n>>16);put16(v,off+2,n);}
 std::vector<uint8_t> encode(uint32_t seq,uint32_t ack,unsigned flags,unsigned window,const std::vector<uint8_t>&payload={}){
  unsigned h=(flags&2)?24:20;std::vector<uint8_t> tcp(h);put16(tcp,0,443);put16(tcp,2,12345);
  put32(tcp,4,seq);put32(tcp,8,ack);tcp[12]=(h/4)<<4;tcp[13]=flags;put16(tcp,14,window);
  if(flags&2){tcp[20]=2;tcp[21]=4;put16(tcp,22,mss);}
  tcp.insert(tcp.end(),payload.begin(),payload.end());std::vector<uint8_t> pseudo(12);
  put32(pseudo,0,0x0a000002);put32(pseudo,4,0x0a000001);pseudo[9]=6;put16(pseudo,10,tcp.size());
  pseudo.insert(pseudo.end(),tcp.begin(),tcp.end());put16(tcp,16,sum(pseudo));
  std::vector<uint8_t> ip(20);ip[0]=0x45;put16(ip,2,20+tcp.size());put16(ip,6,0x4000);ip[8]=64;ip[9]=6;
  put32(ip,12,0x0a000002);put32(ip,16,0x0a000001);put16(ip,10,sum(ip));ip.insert(ip.end(),tcp.begin(),tcp.end());
  std::vector<uint8_t> eth(14);eth[0]=2;eth[5]=1;eth[6]=2;eth[11]=2;put16(eth,12,0x0800);
  eth.insert(eth.end(),ip.begin(),ip.end());if(eth.size()<60)eth.resize(60);return eth;
 }
 void wire(const std::vector<uint8_t>&v,bool lateError=false){
  // For error tests mark the entire frame invalid; the MAC commit boundary
  // must still drain it and keep every byte away from the TCP state machine.
  unsigned target=ingressFrames+ingress.size()+1;ingress.push_back(v);
  d.set_io$$netError(lateError);until([&]{return ingressFrames>=target;});d.set_io$$netError(0);
 }
 std::vector<uint8_t> envelope(unsigned proto,const std::vector<uint8_t>&payload){
  std::vector<uint8_t> ip(20);ip[0]=0x45;put16(ip,2,20+payload.size());put16(ip,6,0x4000);ip[8]=64;ip[9]=proto;
  put32(ip,12,0x0a000002);put32(ip,16,0x0a000001);put16(ip,10,sum(ip));ip.insert(ip.end(),payload.begin(),payload.end());
  std::vector<uint8_t> eth(14);eth[0]=2;eth[5]=1;eth[6]=2;eth[11]=2;put16(eth,12,0x0800);
  eth.insert(eth.end(),ip.begin(),ip.end());if(eth.size()<60)eth.resize(60);return eth;
 }
 std::vector<uint8_t> udpFrame(const std::vector<uint8_t>&payload){
  std::vector<uint8_t> udp(8);put16(udp,0,20000);put16(udp,2,10000);put16(udp,4,8+payload.size());
  udp.insert(udp.end(),payload.begin(),payload.end());std::vector<uint8_t> pseudo(12);
  put32(pseudo,0,0x0a000002);put32(pseudo,4,0x0a000001);pseudo[9]=17;put16(pseudo,10,udp.size());
  pseudo.insert(pseudo.end(),udp.begin(),udp.end());unsigned c=sum(pseudo);put16(udp,6,c?c:65535);return envelope(17,udp);
 }
 void queueEcho(unsigned n,bool corrupt=false){
  std::vector<uint8_t> message(8+n);message[0]=8;put16(message,4,rng());put16(message,6,rng());
  for(unsigned i=8;i<message.size();++i)message[i]=rng();put16(message,2,sum(message));
  if(corrupt){message[4]^=1;ingress.push_back(envelope(1,message));return;}
  auto frame=envelope(1,message);message[0]=0;put16(message,2,0);put16(message,2,sum(message));
  echoExpected.push_back(message);ingress.push_back(frame);
 }
 void sendUdp(const std::vector<uint8_t>&v){
  d.set_io$$udpLength(v.size());d.set_io$$udpId(udpTxPackets);d.set_io$$udpSendValid(1);
  until([&]{return d.get_io$$udpSendReady();});d.set_io$$udpSendValid(0);udpTxExpected.push_back(v);
  for(unsigned off=0;off<v.size()||off==0;off+=16){
   unsigned n=std::min(16U,unsigned(v.size())-off);uint64_t lo=0,hi=0;
   for(unsigned i=0;i<n;++i)(i<8?lo:hi)|=uint64_t(v[off+i])<<(8*(i%8));
   d.set_io$$udpLo(lo);d.set_io$$udpHi(hi);d.set_io$$udpKeep((1U<<n)-1);d.set_io$$udpLast(off+n==v.size());
   d.set_io$$udpValid(1);until([&]{return d.get_io$$udpReady();});d.set_io$$udpValid(0);if(v.empty())break;
  }
 }
 void rx(uint32_t seq,uint32_t ack,unsigned flags=16,unsigned window=65535){
  wire(encode(seq,ack,flags,window));idle(160);
 }
 void receiveData(const std::vector<uint8_t>&v,unsigned skipped=0){
  uint32_t start=peerNext-skipped;for(unsigned i=skipped;i<v.size();++i)expectedRx.push_back(v[i]);
  peerNext+=v.size()-skipped;wire(encode(start,planned,24,65535,v));idle(400);
  check(d.get_io$$receiveNext()==peerNext,"receive sequence did not advance");
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
