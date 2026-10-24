#pragma once
#include "gsim_model_init.h"
#include <algorithm>
#include <array>
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
static void put16(std::vector<uint8_t>&v,unsigned p,unsigned n){v.at(p)=n>>8;v.at(p+1)=n;}
static void put32(std::vector<uint8_t>&v,unsigned p,uint32_t n){put16(v,p,n>>16);put16(v,p+2,n);}
static uint16_t sum(std::vector<uint8_t> v){if(v.size()%2)v.push_back(0);uint32_t n=0;
 for(unsigned i=0;i<v.size();i+=2)n+=u16(v,i);while(n>>16)n=(n&65535)+(n>>16);return uint16_t(~n);}
struct MultiMeta {unsigned connection;uint32_t tag,gen,end;bool retry,data,consumes;uint32_t dst;uint64_t context;auto operator<=>(const MultiMeta&)const=default;};
struct MultiPacket {MultiMeta meta;std::vector<uint8_t> bytes;unsigned cycle;};
struct MultiResult {uint32_t tag,gen;bool failed=false;};
struct MultiBlock {unsigned connection;uint32_t seq;std::vector<uint8_t> bytes;};
template<class Harness,bool Ethernet=false> struct MultiTcpPeer {
 Harness d;std::mt19937_64 rng{0x24a971b3};
 std::array<uint32_t,4> remoteIp{0x0a000002,0x0a000003,0x0a000004,0x0a000005};
 std::array<unsigned,4> remotePort{8000,8001,8002,8003},localPort{12345,12345,12345,12345},mss{1460,1460,1460,1460};
 std::array<uint32_t,4> planned{},peerNext{},generation{};
 std::array<unsigned,4> receivedBytes{},stored{},dataPackets{},replayed{};
 std::array<std::deque<uint8_t>,4> expectedRx;
 std::deque<MultiPacket> pending;std::vector<MultiPacket> packets;
 std::map<uint64_t,MultiBlock> blocks;
 using Beat=std::tuple<uint64_t,uint64_t,unsigned,bool,bool,bool,MultiMeta>;
 std::optional<Beat> held;std::optional<MultiMeta> current;bool currentTcp=false;
 std::vector<std::vector<uint8_t>> otherPackets;
 unsigned tcpErrors=0,udpErrors=0,udpLocalErrors=0; unsigned lastTcpErrorMtu=0,lastUdpErrorMtu=0;
 std::vector<uint8_t> bytes,udpBytes;std::optional<MultiResult> injection;
 bool autoComplete=true,stallOutput=false,random=true;
 unsigned stallReceiveMask=0,cycles=0,completed=0,invalid=0,drops=0,busyDrops=0,arpFrames=0,icmpFrames=0;
 uint64_t nextContext=0;int fixedRead=-1;
 bool autoAcknowledge=false;unsigned automaticOffset=0,maxAckQueue=0;
 std::deque<std::vector<uint8_t>> automaticInput;
 MultiTcpPeer(){
  initializeGsimModel(d, &Harness::init);
  d.set_io$$exceptionReady(0);d.set_io$$udpCommandValid(0);d.set_io$$udpDataValid(0);
  d.set_io$$appConnection(0);d.set_io$$readConnection(0);
  d.set_io$$netValid(0);d.set_io$$netError(0);d.set_io$$receivedReady(0);
  d.set_io$$openMask(0);d.set_io$$closeMask(0);d.set_io$$allocateValid(0);d.set_io$$valid(0);
  d.set_io$$completedValid(0);d.set_io$$outReady(0);d.set_io$$storedReadyMask(15);
  d.set_io$$congestionWindow(65535);d.set_io$$noticeReadyMask(15);d.set_io$$tick(0);
  d.set_io$$remoteIp(remoteIp[0]);d.set_io$$localPort(localPort[0]);d.set_io$$remotePort(remotePort[0]);
  d.set_io$$passive(0);d.set_io$$abort(0);d.set_io$$udpReady(1);
  d.set_reset(1);d.step();d.step();d.set_reset(0);idle(5);
 }
 MultiMeta meta(){return {unsigned(d.get_io$$packetConnection()),uint32_t(d.get_io$$packetTag()),
  uint32_t(d.get_io$$packetGeneration()),uint32_t(d.get_io$$packetEnd()),bool(d.get_io$$packetRetry()),
  bool(d.get_io$$packetData()),bool(d.get_io$$packetConsumes()),uint32_t(d.get_io$$packetDestination()),d.get_io$$packetContext()};}
 unsigned state(unsigned c){return (unsigned(d.get_io$$states())>>(c*4))&15;}
 void select(unsigned c){d.set_io$$appConnection(c);step();}
 void validate(const MultiPacket&p){
  auto&v=p.bytes;unsigned c=p.meta.connection;check(c<4,"bad connection ID");
  check(v.size()>=40&&v[0]==0x45&&u16(v,2)==v.size(),"bad IPv4 length");
  check(sum({v.begin(),v.begin()+20})==0&&v[9]==6&&v[8]==64,"bad IPv4 checksum/protocol/TTL");
  check(u32(v,12)==0x0a000001&&u32(v,16)==remoteIp[c]&&p.meta.dst==remoteIp[c],"cross-connection IP tuple");
  check(u16(v,20)==localPort[c]&&u16(v,22)==remotePort[c],"cross-connection TCP tuple");
  std::vector<uint8_t> pseudo(v.begin()+12,v.begin()+20);pseudo.push_back(0);pseudo.push_back(6);
  unsigned len=v.size()-20;pseudo.push_back(len>>8);pseudo.push_back(len);
  pseudo.insert(pseudo.end(),v.begin()+20,v.end());check(sum(pseudo)==0,"bad TCP checksum");
  unsigned h=(v[32]>>4)*4;check(h>=20&&h<=60&&v.size()>=20+h,"bad TCP offset");
  unsigned n=v.size()-20-h;
  check(p.meta.consumes==bool(n||(v[33]&3)),"sequence-space descriptor wrong");
  check(p.meta.end==uint32_t(u32(v,24)+n+bool(v[33]&2)+bool(v[33]&1)),"TCP descriptor end wrong");
  if(p.meta.data){
   ++dataPackets[c];if(p.meta.retry)++replayed[c];
   auto it=blocks.find(p.meta.context);check(it!=blocks.end(),"unknown application context");
   auto&b=it->second;check(b.connection==c,"cross-connection store context");
   unsigned off=u32(v,24)-b.seq;
   check(n&&n<=mss[c]&&off+n<=b.bytes.size(),"bad TCP segmentation");
   check(std::equal(v.begin()+20+h,v.end(),b.bytes.begin()+off),"cross-connection/retransmit payload corruption");
  }else check(n==0,"unexpected control payload");
 }
 void step(bool tick=false){
  bool ready=!stallOutput&&(!random||rng()%4);d.set_io$$outReady(ready);d.set_io$$tick(tick);
  unsigned read=fixedRead<0?cycles%4:unsigned(fixedRead);
  bool readReady=!(stallReceiveMask&(1U<<read))&&(!random||rng()%4);
  d.set_io$$readConnection(read);d.set_io$$receivedReady(readReady);
  auto r=injection;injection.reset();
  if(!r&&autoComplete&&!pending.empty()&&(!random||rng()%3))r=MultiResult{pending.front().meta.tag,pending.front().meta.gen};
  d.set_io$$completedValid(bool(r));
  if(r){d.set_io$$completedTag(r->tag);d.set_io$$completedGeneration(r->gen);d.set_io$$completedFailed(r->failed);}
  if(autoAcknowledge){
   d.set_io$$netValid(!automaticInput.empty());d.set_io$$netError(0);
   if(!automaticInput.empty()){
    auto&v=automaticInput.front();unsigned n=std::min(16U,unsigned(v.size())-automaticOffset);
    uint64_t lo=0,hi=0;
    for(unsigned i=0;i<n;++i)(i<8?lo:hi)|=uint64_t(v[automaticOffset+i])<<(8*(i%8));
    d.set_io$$netLo(lo);d.set_io$$netHi(hi);d.set_io$$netKeep((1U<<n)-1);
    d.set_io$$netLast(automaticOffset+n==v.size());
   }
  }
  d.step();++cycles;
  if(autoAcknowledge&&!automaticInput.empty()&&d.get_io$$netReady()){
   automaticOffset+=std::min(16U,unsigned(automaticInput.front().size())-automaticOffset);
   if(automaticOffset==automaticInput.front().size()){automaticInput.pop_front();automaticOffset=0;}
  }
  if(d.get_io$$fault()){std::cerr<<"multi fault cycle="<<cycles<<" states="<<std::hex<<d.get_io$$states()<<std::dec<<'\n';throw std::runtime_error("multi core fault");}
  if(r){
   if(d.get_io$$completionAccepted()){
    check(!pending.empty()&&pending.front().meta.tag==r->tag&&pending.front().meta.gen==r->gen,"wrong global completion accepted");
    pending.pop_front();++completed;
   }else {check(d.get_io$$invalidCompletion(),"completion lost without rejection");++invalid;}
  }
  Beat beat={d.get_io$$outLo(),d.get_io$$outHi(),unsigned(d.get_io$$outKeep()),bool(d.get_io$$outLast()),
    bool(d.get_io$$outError()),bool(d.get_io$$txTcp()),d.get_io$$txTcp()?meta():MultiMeta{}};
  if(held)check(d.get_io$$outValid()&&beat==*held,"shared TX changed while stalled");
  held=d.get_io$$outValid()&&!ready?std::optional<Beat>(beat):std::nullopt;
  if(d.get_io$$outValid()&&ready){
   check(!d.get_io$$outError(),"serialized stream error");
   if(!current){current=meta();currentTcp=d.get_io$$txTcp();}
   if(currentTcp)check(meta()==*current&&d.get_io$$txTcp(),"connection descriptor changed mid-frame");
   unsigned keep=d.get_io$$outKeep(),n=__builtin_popcount(keep);check(keep&&(keep&(keep+1))==0,"nonprefix keep");
   for(unsigned i=0;i<n;++i)bytes.push_back((i<8?d.get_io$$outLo():d.get_io$$outHi())>>(8*(i%8)));
   if(d.get_io$$outLast()){
    if constexpr(Ethernet){
     check(bytes.size()>=42,"short Ethernet output");
     check(std::equal(bytes.begin()+6,bytes.begin()+12,std::vector<uint8_t>{2,0,0,0,0,1}.begin()),"bad local source MAC");
     unsigned type=u16(bytes,12);
     if(currentTcp){
      check(type==0x0800&&bytes[23]==6,"TCP metadata on other protocol");
      bytes=std::vector<uint8_t>(bytes.begin()+14,bytes.begin()+14+u16(bytes,16));
     }else if(type==0x0806)++arpFrames;
     else {
      check(type==0x0800,"unexpected non-IP egress");
      auto ip=std::vector<uint8_t>(bytes.begin()+14,bytes.begin()+14+u16(bytes,16));
      check(ip.size()>=20&&sum({ip.begin(),ip.begin()+20})==0,"bad exception/UDP IPv4 header");
      if(ip[9]==1){++icmpFrames;check(sum({ip.begin()+20,ip.end()})==0,"bad ICMP checksum");
       check(ip[20]==0||ip[20]==3,"unexpected generated ICMP type");
      }else if(ip[9]==6||ip[9]==17){
       unsigned len=ip.size()-20;
       std::vector<uint8_t>pseudo(ip.begin()+12,ip.begin()+20);pseudo.push_back(0);pseudo.push_back(ip[9]);
       pseudo.push_back(len>>8);pseudo.push_back(len);pseudo.insert(pseudo.end(),ip.begin()+20,ip.end());
       check(sum(pseudo)==0,"bad stateless TCP/UDP checksum");
       if(ip[9]==6)check(ip.size()==40&&(ip[33]&4),"non-owned TCP must be a stateless reset");
       else check(u16(ip,24)==len,"UDP wire length mismatch");
      }else check(false,"unexpected IP protocol");
      otherPackets.push_back(ip);
     }
    }
    if(currentTcp){
     MultiPacket p{*current,bytes,cycles};validate(p);pending.push_back(p);packets.push_back(p);
     if(autoAcknowledge&&p.meta.data){
      unsigned c=p.meta.connection;auto ack=encode(c,peerNext[c],p.meta.end);
      if constexpr(Ethernet)ack=wrap(ack,c);
      automaticInput.push_back(ack);maxAckQueue=std::max(maxAckQueue,unsigned(automaticInput.size()));
     }
    }
    bytes.clear();current.reset();
   }
  }
  for(unsigned c=0;c<4;++c)if((d.get_io$$storedValid())&(1U<<c)){
   check(!(d.get_io$$storedError()&(1U<<c)),"store failed");++stored[c];
  }
  if(d.get_io$$networkErrorValid()){++tcpErrors;lastTcpErrorMtu=d.get_io$$networkErrorMtu();}
  if(d.get_io$$udpNoticeValid()){++udpErrors;udpLocalErrors+=d.get_io$$udpNoticeLocal();lastUdpErrorMtu=d.get_io$$udpNoticeMtu();}
  drops+=d.get_io$$rxDrops();busyDrops+=d.get_io$$rxBusyDropped();
  if(d.get_io$$receivedValid()&&readReady){
   unsigned n=__builtin_popcount(d.get_io$$receivedKeep());check(n&&expectedRx[read].size()>=n,"unexpected application receive data");
   for(unsigned i=0;i<n;++i){uint8_t b=(i<8?d.get_io$$receivedLo():d.get_io$$receivedHi())>>(8*(i%8));
    check(b==expectedRx[read].front(),"cross-connection receive corruption");expectedRx[read].pop_front();++receivedBytes[read];}
  }
  if(d.get_io$$udpValid()){
   for(unsigned i=0;i<unsigned(__builtin_popcount(d.get_io$$udpKeep()));++i)
    udpBytes.push_back((i<8?d.get_io$$udpLo():d.get_io$$udpHi())>>(8*(i%8)));
  }
 }
 void idle(unsigned n,bool tick=false){while(n--)step(tick);}
 template<class F>void until(F f,unsigned limit=30000){for(unsigned i=0;i<limit;++i){step();if(f())return;}
  std::cerr<<"wait cycles="<<cycles<<" states="<<std::hex<<d.get_io$$states()<<std::dec<<" pending="<<pending.size()<<" outstanding="<<d.get_io$$outstanding()<<'\n';
  throw std::runtime_error("multi wait timed out");}
 void drain(){idle(180);until([&]{return pending.empty()&&!d.get_io$$outstanding()&&!d.get_io$$outValid();});idle(40);}
 std::vector<uint8_t> encode(unsigned c,uint32_t seq,uint32_t ack,unsigned flags=16,unsigned window=65535,
   const std::vector<uint8_t>&payload={},const std::vector<uint8_t>&options={}){
  auto opt=options;if((flags&2)&&opt.empty())opt={2,4,uint8_t(mss[c]>>8),uint8_t(mss[c])};
  while(opt.size()%4)opt.push_back(1);unsigned h=20+opt.size();
  std::vector<uint8_t> tcp(h);put16(tcp,0,remotePort[c]);put16(tcp,2,localPort[c]);
  put32(tcp,4,seq);put32(tcp,8,ack);tcp[12]=(h/4)<<4;tcp[13]=flags;put16(tcp,14,window);
  std::copy(opt.begin(),opt.end(),tcp.begin()+20);tcp.insert(tcp.end(),payload.begin(),payload.end());
  std::vector<uint8_t> pseudo(12);put32(pseudo,0,remoteIp[c]);put32(pseudo,4,0x0a000001);
  pseudo[9]=6;put16(pseudo,10,tcp.size());pseudo.insert(pseudo.end(),tcp.begin(),tcp.end());put16(tcp,16,sum(pseudo));
  std::vector<uint8_t> ip(20);ip[0]=0x45;put16(ip,2,20+tcp.size());put16(ip,6,0x4000);ip[8]=64;ip[9]=6;
  put32(ip,12,remoteIp[c]);put32(ip,16,0x0a000001);put16(ip,10,sum(ip));ip.insert(ip.end(),tcp.begin(),tcp.end());return ip;
 }
 std::vector<uint8_t> wrap(const std::vector<uint8_t>&ip,unsigned c=0,unsigned type=0x0800){
  std::vector<uint8_t> frame={2,0,0,0,0,1,2,0,0,0,0,uint8_t(c+2),uint8_t(type>>8),uint8_t(type)};
  frame.insert(frame.end(),ip.begin(),ip.end());if(frame.size()<60)frame.resize(60);return frame;
 }
 void rawWire(const std::vector<uint8_t>&v,bool lateError=false){
  for(unsigned off=0;off<v.size();off+=16){
   unsigned n=std::min(16U,unsigned(v.size())-off);uint64_t lo=0,hi=0;
   for(unsigned i=0;i<n;++i)(i<8?lo:hi)|=uint64_t(v[off+i])<<(8*(i%8));
   d.set_io$$netLo(lo);d.set_io$$netHi(hi);d.set_io$$netKeep((1U<<n)-1);
   d.set_io$$netLast(off+n==v.size());d.set_io$$netError(lateError&&off+n==v.size());d.set_io$$netValid(1);
   until([&]{return d.get_io$$netReady();});d.set_io$$netValid(0);
  }d.set_io$$netError(0);
 }
 void wire(const std::vector<uint8_t>&v,bool lateError=false,unsigned c=0){
  if constexpr(Ethernet)rawWire(wrap(v,c),lateError);else rawWire(v,lateError);
 }
 void rx(unsigned c,uint32_t seq,uint32_t ack,unsigned flags=16,unsigned window=65535){
  wire(encode(c,seq,ack,flags,window),false,c);idle(160);
 }
 void open(unsigned c,uint32_t seq,bool passive=false,unsigned smss=1460){
  mss[c]=smss;planned[c]=seq+1;peerNext[c]=0;
  d.set_io$$appConnection(c);d.set_io$$initialSequence(seq);d.set_io$$localMss(smss);d.set_io$$passive(passive);
  d.set_io$$remoteIp(passive?0:remoteIp[c]);d.set_io$$localPort(localPort[c]);d.set_io$$remotePort(passive?0:remotePort[c]);
  d.set_io$$openMask(1U<<c);until([&]{return d.get_io$$openReady()&(1U<<c);});d.set_io$$openMask(0);idle(5);
  generation[c]=d.get_io$$generation();
 }
 void active(unsigned c,uint32_t seq,uint32_t peer,unsigned smss=1460){
  open(c,seq,false,smss);until([&]{return d.get_io$$sendActual()==seq+1;});
  peerNext[c]=peer+1;rx(c,peer,seq+1,18);check(state(c)==4,"active handshake failed");drain();
 }
 uint64_t write(unsigned c,unsigned n){
  MultiBlock b{c,planned[c],std::vector<uint8_t>(n)};for(auto&v:b.bytes)v=rng();
  uint64_t id=++nextContext;blocks.emplace(id,b);planned[c]+=n;
  unsigned before=stored[c];d.set_io$$appConnection(c);d.set_io$$length(n);d.set_io$$context(id);d.set_io$$allocateValid(1);
  until([&]{return d.get_io$$allocateReady();});d.set_io$$allocateValid(0);
  for(unsigned off=0;off<n||off==0;off+=16){
   unsigned count=std::min(16U,n-off);uint64_t lo=0,hi=0;
   for(unsigned i=0;i<count;++i)(i<8?lo:hi)|=uint64_t(b.bytes[off+i])<<(8*(i%8));
   d.set_io$$lo(lo);d.set_io$$hi(hi);d.set_io$$keep((1U<<count)-1);d.set_io$$last(off+count==n);d.set_io$$error(0);d.set_io$$valid(1);
   until([&]{return d.get_io$$ready();});d.set_io$$valid(0);if(!n)break;
  }until([&]{return stored[c]>before;});return id;
 }
 void ack(unsigned c){
  select(c);
  for(unsigned n=0;n<64;++n){
   auto actual=uint32_t(d.get_io$$sendActual());rx(c,peerNext[c],actual);select(c);
   if(d.get_io$$sendOldest()==planned[c])return;
  }throw std::runtime_error("cumulative ACK progress did not drain application write");
 }
 void expect(unsigned c,const std::vector<uint8_t>&v){for(auto b:v)expectedRx[c].push_back(b);}
 std::vector<uint8_t> pattern(unsigned n){std::vector<uint8_t>v(n);for(auto&b:v)b=rng();return v;}
 void receive(unsigned c,const std::vector<uint8_t>&v){
  expect(c,v);uint32_t seq=peerNext[c];peerNext[c]+=v.size();
  wire(encode(c,seq,planned[c],24,65535,v),false,c);idle(300);
 }
 void close(unsigned c,bool abort=true){
  d.set_io$$closeMask(1U<<c);d.set_io$$abort(abort);until([&]{return d.get_io$$closeReady()&(1U<<c);});
  d.set_io$$closeMask(0);idle(20);
 }
 void learn(unsigned c){
  std::vector<uint8_t>arp(28);put16(arp,0,1);put16(arp,2,0x0800);arp[4]=6;arp[5]=4;put16(arp,6,2);
  arp[8]=2;arp[13]=c+2;put32(arp,14,remoteIp[c]);arp[18]=2;arp[23]=1;put32(arp,24,0x0a000001);
  rawWire(wrap(arp,c,0x0806));idle(30);
 }
};
