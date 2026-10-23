#include "TcpRingHarness.h"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>
#include <tuple>
#include <vector>
static void check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
int main(){try{
 STcpRingHarness d;std::mt19937_64 rng{0x425952494e47};
 unsigned cycles=0,reservations=0,bytesRead=0,concurrent=0,resets=0,stalls=0;
 for(unsigned trial=0;trial<20;++trial){
  d.set_io$$reserveValid(0);d.set_io$$valid(0);d.set_io$$outReady(0);
  d.set_reset(1);d.step();d.step();d.set_reset(0);++resets;
  std::deque<uint8_t> expected;std::vector<uint8_t> block;
  unsigned pos=0,used=0,n=0,request=0;bool writing=false,driving=false;
  uint64_t lo=0,hi=0;
  using Beat=std::tuple<uint64_t,uint64_t,unsigned>;
  std::optional<Beat> held;
  for(unsigned c=0;c<15000||used||writing;++c){
   if(!writing&&!request&&c<15000){request=1+rng()%1024;if(c%3==0)request=1+rng()%17;}
   bool av=bool(request),rd=rng()%5!=0;
   // Long reader stalls force full capacity and write-pointer wrap.
   if(c<15000&&c%1000<200)rd=false;
   if(writing&&!driving&&rng()%4){
    n=std::min(16U,unsigned(block.size())-pos);lo=hi=0;
    for(unsigned i=0;i<n;++i)(i<8?lo:hi)|=uint64_t(block[pos+i])<<(8*(i%8));
    driving=true;
   }
   d.set_io$$reserveValid(av);d.set_io$$length(request);d.set_io$$valid(driving);
   d.set_io$$lo(lo);d.set_io$$hi(hi);d.set_io$$keep((1U<<n)-1);d.set_io$$last(writing&&pos+n==block.size());
   d.set_io$$outReady(rd);d.step();++cycles;
   check(!d.get_io$$fault(),"ring input fault");check(d.get_io$$free()==1024-used,"byte reservation credit mismatch");
   check(bool(d.get_io$$empty())==(used==0),"empty ignores outstanding reservations");
   Beat beat={d.get_io$$outLo(),d.get_io$$outHi(),unsigned(d.get_io$$outKeep())};
   if(held)check(d.get_io$$outValid()&&beat==*held,"ring output changed under backpressure");
   held=d.get_io$$outValid()&&!rd?std::optional<Beat>(beat):std::nullopt;
   bool read=d.get_io$$outValid()&&rd;
   if(read){
    unsigned count=__builtin_popcount(d.get_io$$outKeep());
    check(count&&expected.size()>=count,"read uncommitted or unreserved bytes");
    for(unsigned i=0;i<count;++i){uint8_t b=(i<8?d.get_io$$outLo():d.get_io$$outHi())>>(8*(i%8));
     check(b==expected.front(),"byte order / pointer wrap corruption");expected.pop_front();}
    used-=count;bytesRead+=count;
   }
   if(driving&&d.get_io$$ready()){
    for(unsigned i=0;i<n;++i)expected.push_back(block[pos+i]);
    pos+=n;driving=false;if(read)++concurrent;
    if(pos==block.size())writing=false;
   }
   if(av&&d.get_io$$reserveReady()){
    check(!writing&&request<=1024-used,"reservation without space");
    used+=request;block.resize(request);for(auto&v:block)v=rng();pos=0;writing=true;request=0;++reservations;
   }
   if(av&&!d.get_io$$reserveReady())++stalls;
   check(c<50000,"ring failed to drain");
  }
  check(expected.empty(),"ring lost bytes on drain");
  // Cancel a reserved but unwritten range across reset.
  d.set_io$$reserveValid(1);d.set_io$$length(101);d.set_io$$valid(0);d.step();
  d.set_io$$reserveValid(0);d.step();
 }
 std::cout<<"TCP_RECEIVE_RING_PASS cycles="<<cycles<<" reservations="<<reservations<<" bytes="<<bytesRead
  <<" concurrent="<<concurrent<<" full_stalls="<<stalls<<" resets="<<resets
  <<" byte_packing=pass wrap=pass reserved_credit=pass backpressure=pass\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
