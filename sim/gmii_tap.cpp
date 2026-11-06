#include "IperfGmiiHarness.h"
#include <linux/if_tun.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <csignal>
#include <chrono>
#include <deque>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <cstdio>
#include <cstring>

static uint32_t crc(const std::vector<uint8_t>&v,size_t start,size_t end){
 uint32_t c=0xffffffff;
 for(size_t i=start;i<end;i++){c^=v[i];for(int n=0;n<8;n++)c=(c>>1)^((c&1)?0xedb88320:0);}
 return ~c;
}
static std::vector<uint8_t> wire(std::vector<uint8_t> v,bool corrupt=false){
 if(v.size()<60)v.resize(60);
 uint32_t f=crc(v,0,v.size());if(corrupt)f^=1;
 for(int i=0;i<4;i++)v.push_back(f>>(8*i));
 v.insert(v.begin(),{0x55,0x55,0x55,0x55,0x55,0x55,0x55,0xd5});return v;
}
static unsigned be16(const std::vector<uint8_t>&v,unsigned p){return(unsigned(v.at(p))<<8)|v.at(p+1);}
static bool stop=false;
static void interrupt(int){stop=true;}
int main(int argc,char**argv){try{
 signal(SIGTERM,interrupt);signal(SIGINT,interrupt);
 int fd=open("/dev/net/tun",O_RDWR|O_NONBLOCK);
 if(fd<0)throw std::runtime_error("cannot open tun");
 ifreq ifr{};ifr.ifr_flags=IFF_TAP|IFF_NO_PI;
 std::strncpy(ifr.ifr_name,argc>1?argv[1]:"viperf0",IFNAMSIZ-1);
 if(ioctl(fd,TUNSETIFF,&ifr))throw std::runtime_error("TUNSETIFF failed");
 SIperfGmiiHarness d;
 d.set_io$$link(1);d.set_io$$tick(0);d.set_io$$rxData(0);d.set_io$$rxValid(0);d.set_io$$rxError(0);
 d.set_reset(1);d.step();d.step();d.set_reset(0);for(int i=0;i<8;i++)d.step();
 std::deque<std::vector<uint8_t>> ingress,egress;
 std::deque<bool> ingressBad;
 std::vector<uint8_t> tx;
 unsigned offset=0,gap=0; bool badInjected=false,droppedData=false,waitingBadDrop=false,badDropConfirmed=false;
 uint64_t badDropBefore=0;
 uint64_t cycles=0,rxFrames=0,txFrames=0,rxDropped=0;
 unsigned previousPhase=255;
 auto start=std::chrono::steady_clock::now(),last=start;
 std::cout<<"READY "<<ifr.ifr_name<<std::endl;
 while(!stop&&std::chrono::steady_clock::now()-start<std::chrono::seconds(180)){
   if((cycles&63)==0){
     uint8_t b[65536];ssize_t n=read(fd,b,sizeof(b));
     if(n>0){
       std::vector<uint8_t> v(b,b+n);
       if(!badInjected&&v.size()>=42&&be16(v,12)==0x806){
         ingress.push_back(wire(v,true));ingressBad.push_back(true);badInjected=true;
       }
       ingress.push_back(wire(v));ingressBad.push_back(false);rxFrames++;
     }
     if(!egress.empty()){
       auto&v=egress.front();ssize_t n=write(fd,v.data(),v.size());
       if(n==ssize_t(v.size()))egress.pop_front();
       else if(n>=0||errno!=EAGAIN)throw std::runtime_error("short TAP write");
     }
   }
   bool offering=!gap&&!ingress.empty();
   d.set_io$$rxValid(offering);d.set_io$$rxData(offering?ingress.front()[offset]:0);
   auto now=std::chrono::steady_clock::now();
   bool tick=now-last>=std::chrono::milliseconds(1);if(tick)last+=std::chrono::milliseconds(1);
   d.set_io$$tick(tick);d.step();cycles++;
   if(offering){if(++offset==ingress.front().size()){if(ingressBad.front()){waitingBadDrop=true;badDropBefore=rxDropped;}
       ingressBad.pop_front();ingress.pop_front();offset=0;gap=12;}}
   else if(gap)gap--;
   if(d.get_io$$rxDropped())rxDropped++;
   if(waitingBadDrop&&rxDropped>badDropBefore){badDropConfirmed=true;waitingBadDrop=false;}
   if(d.get_io$$fault())throw std::runtime_error("protocol completion/ownership fault");
   if(d.get_io$$txError())throw std::runtime_error("MAC TX_ER asserted");
   if(d.get_io$$txEnable())tx.push_back(d.get_io$$txData());
   else if(!tx.empty()){
     if(tx.size()<72||tx[7]!=0xd5)throw std::runtime_error("short wire frame/SFD");
     for(int i=0;i<7;i++)if(tx[i]!=0x55)throw std::runtime_error("bad preamble");
     uint32_t actual=0;for(int i=0;i<4;i++)actual|=uint32_t(tx[tx.size()-4+i])<<(8*i);
     if(actual!=crc(tx,8,tx.size()-4))throw std::runtime_error("invalid egress Ethernet FCS");
     std::vector<uint8_t> frame(tx.begin()+8,tx.end()-4);
     bool drop=false;
     // Lose a physically transmitted full data segment once. Linux must recover
     // through this RTL stack's ACK/retransmission handling.
     if(!droppedData&&frame.size()>1100&&be16(frame,12)==0x800&&frame[23]==6){
       droppedData=true;drop=true;std::cout<<"INJECT dropped TX TCP segment"<<std::endl;
     }
     if(!drop)egress.push_back(frame);
     tx.clear();txFrames++;
   }
   unsigned phase=d.get_io$$phase();
   if(phase!=previousPhase){
     std::cout<<"phase="<<phase<<" rx="<<d.get_io$$rxBytes()<<" tx="<<d.get_io$$txBytes()
       <<" errors="<<d.get_io$$errors()<<" cycles="<<cycles<<std::endl;
     previousPhase=phase;
   }
 }
 std::cout<<"SUMMARY cycles="<<cycles<<" host_frames="<<rxFrames<<" mac_tx_frames="<<txFrames
   <<" mac_rx_drops="<<rxDropped<<" bad_fcs_injected="<<badInjected<<" network_loss_injected="<<droppedData
   <<" bad_fcs_drop_confirmed="<<badDropConfirmed<<" errors="<<d.get_io$$errors()<<std::endl;
 if(!badDropConfirmed||!badInjected)throw std::runtime_error("bad-FCS rejection not exercised");
 return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}}
