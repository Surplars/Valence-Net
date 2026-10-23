#include "TcpCongestionHarness.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
static void check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
struct Input {bool clear=false,ack=false,handshake=false,duplicate=false,timeout=false,idle=false,ready=true;
 uint32_t mss=1000,previous=100,number=100,oldest=100,flight=6000;};
struct Model {
 uint32_t window=1,threshold=16777215,credit=0,duplicates=0,sequence=0;
 bool recovering=false,pending=false,loss=false,lastTimeout=false;
 static uint32_t cap(uint64_t n){return std::min<uint64_t>(n,16777215);}
 void step(const Input&i){
  uint32_t oldWindow=window;
  uint32_t mss=std::max(i.mss,1U),n=i.number-i.previous;
  bool progress=i.ack&&n&&n<0x80000000U;
  uint32_t reduced=cap(std::max(i.flight/2,2*mss));
  if(i.ready)pending=false;
  if(i.ack){
   if(i.handshake){window=mss;credit=duplicates=0;}
   else if(progress){
    duplicates=0;pending=false;loss=false;
    if(recovering){window=threshold;recovering=false;credit=0;}
    else if(window<threshold)window=cap(uint64_t(window)+std::min(n,mss));
    else{uint64_t c=uint64_t(credit)+n;if(c>=window){window=cap(uint64_t(window)+mss);credit=0;}else credit=c;}
   }else if(i.duplicate){
    if(recovering)window=cap(uint64_t(window)+mss);
    else if(++duplicates==3){threshold=reduced;window=cap(uint64_t(reduced)+3*mss);recovering=true;
     credit=0;pending=true;sequence=i.oldest;}
   }
  }
  if(i.idle&&!i.flight){window=std::min(oldWindow,mss);credit=duplicates=0;recovering=false;}
  if(i.timeout&&!lastTimeout&&!progress){
   if(!loss)threshold=reduced;window=mss;credit=duplicates=0;recovering=false;pending=false;loss=true;
  }
  if(i.clear){window=mss;threshold=16777215;credit=duplicates=0;recovering=loss=pending=false;}
  lastTimeout=i.timeout;
 }
};
int main(){try{
 STcpCongestionHarness d;Model model;Input in;std::mt19937 rng(5681);
 unsigned cycles=0,fast=0,losses=0,stalled=0;
 d.set_reset(1);d.step();d.step();d.set_reset(0);
 auto step=[&]{
  d.set_io$$clear(in.clear);d.set_io$$mss(in.mss);d.set_io$$ackValid(in.ack);
  d.set_io$$previous(in.previous);d.set_io$$acknowledgement(in.number);d.set_io$$handshake(in.handshake);
  d.set_io$$duplicate(in.duplicate);d.set_io$$oldest(in.oldest);d.set_io$$flight(in.flight);
  d.set_io$$timeout(in.timeout);d.set_io$$idleRestart(in.idle);d.set_io$$replayReady(in.ready);d.step();++cycles;
  if(d.get_io$$window()!=model.window||d.get_io$$threshold()!=model.threshold){
   std::cerr<<"cycle="<<cycles<<" got="<<d.get_io$$window()<<"/"<<d.get_io$$threshold()<<" expected="<<model.window<<"/"<<model.threshold
    <<" input ack="<<in.ack<<" delta="<<uint32_t(in.number-in.previous)<<" clear="<<in.clear<<" idle="<<in.idle<<" timeout="<<in.timeout<<'\n';
   throw std::runtime_error("congestion window / threshold mismatch");
  }
  check(bool(d.get_io$$recovering())==model.recovering&&d.get_io$$duplicates()==model.duplicates,"fast recovery state mismatch");
  check(bool(d.get_io$$replayValid())==model.pending,"fast retransmit request mismatch");
  if(model.pending){check(d.get_io$$replaySequence()==model.sequence,"replay sequence changed");if(in.ready)++fast;else ++stalled;}
  if(in.timeout&&!model.lastTimeout)++losses;model.step(in);
 };
 in.clear=true;step();in.clear=false;in.ack=true;in.number=700;step();check(model.window==1600,"ACK division inflated slow start");
 in.handshake=true;step();check(model.window==1000,"SYN ACK grew window");in.handshake=false;in.number=100;in.duplicate=true;in.ready=false;
 step();step();step();check(model.recovering&&model.window==6000&&model.threshold==3000,"three duplicate ACK response");
 in.ack=false;for(int i=0;i<7;++i)step();in.ack=true;step();check(model.window==7000,"recovery duplicate ACK inflation");
 in.duplicate=false;in.number=600;step();check(!model.recovering&&model.window==3000,"recovery progress did not deflate");
 in.ack=false;in.timeout=true;in.flight=10000;step();check(model.threshold==5000&&model.window==1000,"RTO response");
 for(int i=0;i<7;++i)step();in.timeout=false;step();in.timeout=true;in.flight=2000;step();
 check(model.threshold==5000,"repeated RTO halved threshold again");in.timeout=false;in.ack=true;in.number=1100;step();
 in.ack=false;in.timeout=true;in.flight=4000;step();check(model.threshold==2000,"progress did not enable a new loss epoch");
 in.timeout=false;in.ack=true;in.number=1100;step();step();in.ack=false;in.idle=true;in.flight=0;step();
 check(model.window<=1000,"idle restart burst");
 for(unsigned k=0;k<40000;++k){
  in={};in.mss=1+rng()%65495;in.clear=k%701==0;in.ack=rng()%3!=0;
  in.handshake=in.ack&&rng()%211==0;in.previous=rng();in.number=in.previous;
  unsigned choice=rng()%5;if(choice<2)in.number+=1+rng()%65535;else if(choice==2)in.number-=1+rng()%65535;
  in.duplicate=in.ack&&choice>2;in.timeout=rng()%127==0;in.idle=rng()%199==0;
  in.oldest=rng();in.flight=in.idle?0:rng()%100000;in.ready=rng()%3!=0;step();
 }
 in={};step();
 std::cout<<"TCP_CONGESTION_PASS cycles="<<cycles<<" fast_retransmits="<<fast<<" loss_events="<<losses
  <<" request_stalls="<<stalled<<" slow_start=pass byte_counting=pass fast_recovery=pass rto=pass idle_restart=pass\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
