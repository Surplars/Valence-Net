#include "TcpHeaderSnapshotHarness.h"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
static void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static unsigned u16(const std::vector<uint8_t>&v,unsigned p){return unsigned(v.at(p))*256+v.at(p+1);}
static uint32_t u32(const std::vector<uint8_t>&v,unsigned p){return uint32_t(u16(v,p))*65536+u16(v,p+2);}
static uint16_t sum(std::vector<uint8_t>v){if(v.size()%2)v.push_back(0);uint32_t s=0;
 for(unsigned i=0;i<v.size();i+=2)s+=u16(v,i);while(s>>16)s=(s&65535)+(s>>16);return ~s;}
struct Expected{uint32_t seq;unsigned length,options;uint64_t context;bool retry;};
struct Block{uint32_t seq;unsigned length;};
struct Test{
 STcpHeaderSnapshotHarness d;uint32_t next=1000,actual=1000,oldest=1000,replaySequence=1000;
 bool sack=false,flipReserve=false,flipReplay=false,replaying=false,stall=false;
 uint64_t context=0;unsigned stored=0,packets=0,newSnapshots=0,retrySnapshots=0;
 std::map<uint64_t,Block> blocks;std::deque<Expected> expected;std::vector<uint8_t> bytes;
 Test(){
  d.set_io$$allocateValid(0);d.set_io$$valid(0);d.set_io$$replayValid(0);d.set_io$$storedReady(1);
  d.set_io$$reserveReady(1);d.set_io$$enabled(1);d.set_io$$generation(1);d.set_io$$credit(65535);
  d.set_io$$sourceIp(0x0a000001);d.set_io$$destinationIp(0x0a000002);
  d.set_io$$sourcePort(12345);d.set_io$$destinationPort(443);d.set_io$$acknowledgement(256);
  d.set_io$$window(65535);d.set_io$$ttl(64);d.set_io$$tos(0);d.set_io$$outReady(1);
  d.set_reset(1);step();step();d.set_reset(0);for(unsigned i=0;i<5;++i)step();
 }
 void step(){
  bool sampled=sack;
  d.set_io$$sack(sack);d.set_io$$mss(sack?1424:1460);
  d.set_io$$sendOldest(oldest);d.set_io$$sendActual(actual);d.set_io$$sendSequence(next);
  d.set_io$$outReady(!stall);d.set_io$$replayValid(replaying);d.set_io$$replaySequence(replaySequence);
  d.step();check(!d.get_io$$fault(),"ownership/serializer fault");
  if(d.get_io$$reserveValid()){
   unsigned len=d.get_io$$reserveLength();check(len&&len<=(sampled?1424:1460),"MSS budget");
   uint64_t owner=0;for(auto&[id,b]:blocks)if(uint32_t(next-b.seq)<b.length){owner=id;break;}
   check(owner,"reservation has no original bytes");
   expected.push_back({next,len,sampled?36U:0U,owner,false});next+=len;++newSnapshots;
   if(flipReserve){sack=!sack;flipReserve=false;}
  }
  if(replaying&&d.get_io$$replayReady()){
   check(!d.get_io$$replayMiss(),"replay should be retained");
   uint64_t owner=0;unsigned remaining=0;
   for(auto&[id,b]:blocks)if(uint32_t(replaySequence-b.seq)<b.length){owner=id;remaining=b.length-(replaySequence-b.seq);break;}
   check(owner,"replay owner");
   unsigned len=std::min({remaining,sampled?1424U:1460U,unsigned(actual-replaySequence)});
   expected.push_back({replaySequence,len,sampled?36U:0U,owner,true});++retrySnapshots;
   replaying=false;if(flipReplay){sack=!sack;flipReplay=false;}
  }
  if(d.get_io$$storedValid()){check(!d.get_io$$storedError(),"store failure");++stored;}
  if(d.get_io$$outValid()&&!stall){
   unsigned n=__builtin_popcount(d.get_io$$outKeep());
   for(unsigned i=0;i<n;++i)bytes.push_back((i<8?d.get_io$$outLo():d.get_io$$outHi())>>(8*(i%8)));
   if(d.get_io$$outLast()){
    check(!expected.empty(),"unexpected output");auto e=expected.front();expected.pop_front();
    check(bytes.size()<=1500,"MTU overflow: header grew after retained read handshake");
    check(bytes.size()==40+e.options+e.length&&(bytes[32]>>4)*4==20+e.options,"header snapshot changed after read handshake");
    check(u32(bytes,24)==e.seq&&d.get_io$$packetContext()==e.context&&bool(d.get_io$$packetRetransmission())==e.retry,"packet ownership changed");
    check(u16(bytes,2)==bytes.size()&&sum({bytes.begin(),bytes.begin()+20})==0,"IP checksum/length");
    std::vector<uint8_t> pseudo(bytes.begin()+12,bytes.begin()+20);pseudo.push_back(0);pseudo.push_back(6);
    unsigned l=bytes.size()-20;pseudo.push_back(l>>8);pseudo.push_back(l);pseudo.insert(pseudo.end(),bytes.begin()+20,bytes.end());
    check(sum(pseudo)==0,"TCP checksum after header snapshot");
    const auto&b=blocks.at(e.context);unsigned off=e.seq-b.seq;
    for(unsigned i=0;i<e.length;++i)check(bytes[40+e.options+i]==uint8_t((off+i)*7+e.context),"retained payload changed");
    if(!e.retry)actual=e.seq+e.length;
    ++packets;bytes.clear();
   }
  }
 }
 template<class F>void until(F f){for(unsigned i=0;i<20000;++i){step();if(f())return;}throw std::runtime_error("snapshot test timeout");}
 void write(unsigned n){
  ++context;blocks.emplace(context,Block{next,n});unsigned before=stored;
  d.set_io$$length(n);d.set_io$$context(context);d.set_io$$allocateValid(1);
  until([&]{return d.get_io$$allocateReady();});d.set_io$$allocateValid(0);
  for(unsigned off=0;off<n;off+=16){unsigned count=std::min(16U,n-off);uint64_t lo=0,hi=0;
   for(unsigned i=0;i<count;++i)(i<8?lo:hi)|=uint64_t(uint8_t((off+i)*7+context))<<(8*(i%8));
   d.set_io$$lo(lo);d.set_io$$hi(hi);d.set_io$$keep((1U<<count)-1);d.set_io$$last(off+count==n);
   d.set_io$$error(0);d.set_io$$valid(1);until([&]{return d.get_io$$ready();});d.set_io$$valid(0);
  }
  until([&]{return stored>before;});
 }
};
int main(){try{
 Test t;
 // Toggle exactly one cycle after the RAM read/reservation handshake, before
 // its delayed response can be accepted as a serializer command.
 t.flipReserve=true;t.write(1460);t.until([&]{return t.packets==1;});
 check(t.sack,"new-data toggle did not fire");
 t.sack=false;t.flipReplay=true;t.replaying=true;
 t.until([&]{return t.packets==2;});check(t.sack,"replay toggle did not fire");
 // The reverse transition must retain the option snapshot too. Its MSS was
 // already reduced before reservation; no payload may silently grow afterward.
 t.flipReserve=true;t.write(1460);t.until([&]{return t.packets==4;});
 check(t.newSnapshots==3&&t.retrySnapshots==1&&!t.sack,"snapshot coverage");
 std::cout<<"PASS tcp_header_snapshot: exact reserve/replay handshake option changes, both directions, MTU, checksums and retained bytes\n";
 return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL tcp_header_snapshot: "<<e.what()<<'\n';return 1;}}
