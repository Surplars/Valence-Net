#include "AppHarness.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <deque>
#include <vector>
#include <random>
#include <stdexcept>
#include <iostream>
#include <csignal>

static SAppHarness d;
static void set_connected(int i,uint8_t v){if(i==0)d.set_io$$control$$connected(v);else d.set_io$$data$$connected(v);}
static void set_disconnected(int i,uint8_t v){if(i==0)d.set_io$$control$$disconnected(v);else d.set_io$$data$$disconnected(v);}
static void set_rxLo(int i,uint64_t v){if(i==0)d.set_io$$control$$rxLo(v);else d.set_io$$data$$rxLo(v);}
static void set_rxHi(int i,uint64_t v){if(i==0)d.set_io$$control$$rxHi(v);else d.set_io$$data$$rxHi(v);}
static void set_rxKeep(int i,uint16_t v){if(i==0)d.set_io$$control$$rxKeep(v);else d.set_io$$data$$rxKeep(v);}
static void set_rxValid(int i,uint8_t v){if(i==0)d.set_io$$control$$rxValid(v);else d.set_io$$data$$rxValid(v);}
static void set_txReady(int i,uint8_t v){if(i==0)d.set_io$$control$$txReady(v);else d.set_io$$data$$txReady(v);}
static uint8_t get_listening(int i){return i==0?d.get_io$$control$$listening():d.get_io$$data$$listening();}
static uint8_t get_close(int i){return i==0?d.get_io$$control$$close():d.get_io$$data$$close();}
static uint8_t get_rxReady(int i){return i==0?d.get_io$$control$$rxReady():d.get_io$$data$$rxReady();}
static uint64_t get_txLo(int i){return i==0?d.get_io$$control$$txLo():d.get_io$$data$$txLo();}
static uint64_t get_txHi(int i){return i==0?d.get_io$$control$$txHi():d.get_io$$data$$txHi();}
static uint16_t get_txKeep(int i){return i==0?d.get_io$$control$$txKeep():d.get_io$$data$$txKeep();}
static uint8_t get_txValid(int i){return i==0?d.get_io$$control$$txValid():d.get_io$$data$$txValid();}

int main(int argc,char**argv){try{
 signal(SIGPIPE,SIG_IGN);
 int port=argc>1?std::stoi(argv[1]):5201;
 int listener=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK,0),one=1;
 setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
 sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(port);
 if(bind(listener,(sockaddr*)&a,sizeof(a))||listen(listener,4))throw std::runtime_error("listen failed");
 int fd[2]={-1,-1}; bool eof[2]={false,false};
 std::deque<uint8_t> rx[2],tx[2]; unsigned offered[2]={0,0};
 std::mt19937 rng(0x9421);
 for(int i=0;i<2;i++){set_connected(i,0);set_disconnected(i,0);set_rxValid(i,0);set_txReady(i,0);}
 d.set_io$$tick(0);d.set_reset(1);d.step();d.step();d.set_reset(0);
 auto start=std::chrono::steady_clock::now(),last=start;
 unsigned priorPhase=255;uint64_t cycles=0;
 std::cout<<"READY "<<port<<std::endl;
 while(std::chrono::steady_clock::now()-start<std::chrono::seconds(90)){
   for(int i=0;i<2;i++){set_connected(i,0);set_disconnected(i,0);}
   if((cycles&31)==0){
     int accepted=accept4(listener,nullptr,nullptr,SOCK_NONBLOCK);
     if(accepted>=0){
       int i=fd[0]<0&&get_listening(0)?0:fd[1]<0&&get_listening(1)?1:-1;
       if(i<0)close(accepted);
       else {fd[i]=accepted;eof[i]=false;setsockopt(accepted,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));set_connected(i,1);}
     }
     for(int i=0;i<2;i++)if(fd[i]>=0){
       uint8_t b[16384];
       if(rx[i].size()<262144&&!eof[i]){
         ssize_t n=recv(fd[i],b,sizeof(b),0);
         if(n>0)rx[i].insert(rx[i].end(),b,b+n);
         else if(n==0)eof[i]=true;
       }
       if(!tx[i].empty()){
         unsigned n=std::min<size_t>(tx[i].size(),sizeof(b));
         for(unsigned j=0;j<n;j++)b[j]=tx[i][j];
         ssize_t sent=send(fd[i],b,n,MSG_NOSIGNAL);
         if(sent>0)for(int j=0;j<sent;j++)tx[i].pop_front();
       }
     }
   }
   auto now=std::chrono::steady_clock::now();
   bool tick=now-last>=std::chrono::milliseconds(1);
   if(tick)last+=std::chrono::milliseconds(1);
   d.set_io$$tick(tick);
   for(int i=0;i<2;i++){
     if(fd[i]>=0&&get_close(i)&&tx[i].empty()){
       close(fd[i]);fd[i]=-1;rx[i].clear();offered[i]=0;set_disconnected(i,1);
     } else if(fd[i]<0&&get_close(i))set_disconnected(i,1);
     if(!offered[i]&&!rx[i].empty())offered[i]=std::min<size_t>(1+rng()%16,rx[i].size());
     uint64_t lo=0,hi=0;
     for(unsigned j=0;j<offered[i];j++)(j<8?lo:hi)|=uint64_t(rx[i][j])<<(8*(j%8));
     set_rxLo(i,lo);set_rxHi(i,hi);set_rxKeep(i,(1u<<offered[i])-1);set_rxValid(i,offered[i]!=0);
     set_txReady(i,fd[i]>=0&&tx[i].size()<262144&&(rng()%5!=0));
   }
   d.step();cycles++;
   for(int i=0;i<2;i++){
     if(offered[i]&&get_rxReady(i)){for(unsigned j=0;j<offered[i];j++)rx[i].pop_front();offered[i]=0;}
     // The generated model exposes combinational outputs for this handshake.
     bool ready=i==0?d.io$$control$$txReady:d.io$$data$$txReady;
     if(ready&&get_txValid(i)){
       unsigned keep=get_txKeep(i);
       for(unsigned j=0;j<16;j++)if(keep&(1u<<j))tx[i].push_back((j<8?get_txLo(i):get_txHi(i))>>(8*(j%8)));
     }
   }
   unsigned phase=d.get_io$$phase();
   if(phase!=priorPhase){
     std::cout<<"phase="<<phase<<" rx="<<d.get_io$$rxBytes()<<" tx="<<d.get_io$$txBytes()<<" errors="<<d.get_io$$errors()<<std::endl;
     priorPhase=phase;
   }
 }
 std::cout<<"cycles="<<cycles<<std::endl;
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}
