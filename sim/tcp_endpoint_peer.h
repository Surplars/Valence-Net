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
struct Packet {Meta meta;std::vector<uint8_t> bytes;};
struct Result {uint32_t tag,gen;bool failed=false;};
struct Block {uint32_t seq;std::vector<uint8_t> bytes;};
template<class Harness> struct TcpEndpointPeer {
 Harness d;std::mt19937_64 rng{0x10aabbcc};
 std::deque<Packet> pending;std::vector<Packet> packets;std::map<uint64_t,Block> blocks;
 using Beat=std::tuple<uint64_t,uint64_t,unsigned,bool,bool,Meta>;
 std::optional<Beat> held;std::optional<Meta> current;std::vector<uint8_t> bytes;
 std::optional<Result> injection;
 bool autoComplete=true,stallOutput=false,random=true,failNext=false,stallReceive=false;
 std::deque<uint8_t> expectedRx;unsigned receivedBytes=0,drops=0;
 uint32_t planned=0,peerNext=0;unsigned mss=1460;
 unsigned cycles=0,completed=0,invalid=0,dataPackets=0,replayed=0,controls=0,stored=0,timeouts=0;
 unsigned concurrent=0,closes=0,opens=0;uint64_t context=0;
 TcpEndpointPeer(){
  d.set_io$$netValid(0);d.set_io$$netError(0);d.set_io$$receivedReady(0);
  d.set_io$$openValid(0);d.set_io$$closeValid(0);d.set_io$$allocateValid(0);d.set_io$$valid(0);
  d.set_io$$completedValid(0);d.set_io$$outReady(0);d.set_io$$storedReady(1);
  
  d.set_io$$congestionWindow(65535);
  d.set_io$$noticeReady(1);d.set_io$$tick(0);
  d.set_io$$localIp(0x0a000001);d.set_io$$remoteIp(0x0a000002);
  d.set_io$$localPort(12345);d.set_io$$remotePort(443);d.set_io$$offerScale(1);d.set_io$$localScale(0);
  
  
  
  
  
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
   check(n&&n<=mss&&off+n<=b.bytes.size()&&hdr<=60,"invalid segmentation");
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
  d.set_io$$noticeReady(!random||rng()%4);
  std::optional<Result> r=injection;
  if(!r&&autoComplete&&!pending.empty()&&(!random||rng()%3))r=Result{pending.front().meta.tag,pending.front().meta.gen,failNext};
  bool injected=bool(injection);injection.reset();
  d.set_io$$completedValid(bool(r));
  if(r){d.set_io$$completedTag(r->tag);d.set_io$$completedGeneration(r->gen);d.set_io$$completedFailed(r->failed);}
  bool readReady=!stallReceive&&(!random||rng()%4);d.set_io$$receivedReady(readReady);
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
  drops+=d.get_io$$rxDrops();
  if(d.get_io$$receivedValid()&&readReady){
   unsigned n=__builtin_popcount(d.get_io$$receivedKeep());check(n&&expectedRx.size()>=n,"unexpected received bytes");
   for(unsigned i=0;i<n;++i){uint8_t b=(i<8?d.get_io$$receivedLo():d.get_io$$receivedHi())>>(8*(i%8));
    check(b==expectedRx.front(),"receive byte stream corrupted");expectedRx.pop_front();++receivedBytes;}
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
  put32(ip,12,0x0a000002);put32(ip,16,0x0a000001);put16(ip,10,sum(ip));ip.insert(ip.end(),tcp.begin(),tcp.end());return ip;
 }
 void wire(const std::vector<uint8_t>&v,bool lateError=false){
  for(unsigned off=0;off<v.size();off+=16){
   unsigned n=std::min(16U,unsigned(v.size())-off);uint64_t lo=0,hi=0;
   for(unsigned i=0;i<n;++i)(i<8?lo:hi)|=uint64_t(v[off+i])<<(8*(i%8));
   d.set_io$$netLo(lo);d.set_io$$netHi(hi);d.set_io$$netKeep((1U<<n)-1);d.set_io$$netLast(off+n==v.size());
   d.set_io$$netError(lateError&&off+n==v.size());d.set_io$$netValid(1);until([&]{return d.get_io$$netReady();});d.set_io$$netValid(0);
  }
  d.set_io$$netError(0);
 }
 void rx(uint32_t seq,uint32_t ack,unsigned flags=16,unsigned window=65535){
  wire(encode(seq,ack,flags,window));idle(100);
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
