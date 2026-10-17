#include "TransmitHarness.h"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
static void check(bool ok,const char*s){if(!ok)throw std::runtime_error(s);}
static void put16(std::vector<uint8_t>&v,unsigned p,uint16_t n){v[p]=n>>8;v[p+1]=n;}
static void put32(std::vector<uint8_t>&v,unsigned p,uint32_t n){put16(v,p,n>>16);put16(v,p+2,n);}
static uint16_t checksum(std::vector<uint8_t>v){
    if(v.size()%2)v.push_back(0);uint32_t s=0;
    for(unsigned i=0;i<v.size();i+=2)s+=(v[i]<<8)|v[i+1];
    while(s>>16)s=(s&65535)+(s>>16);return uint16_t(~s);
}
struct Packet{
    std::vector<uint8_t> payload,wire;
    uint32_t src,dst;uint16_t sport,dport,length,ident;uint8_t ttl,tos;unsigned id,mode;bool valid;
};
static uint16_t udpChecksum(const Packet&p){
    std::vector<uint8_t> bytes(20);put32(bytes,0,p.src);put32(bytes,4,p.dst);bytes[9]=17;
    put16(bytes,10,p.payload.size()+8);put16(bytes,12,p.sport);put16(bytes,14,p.dport);put16(bytes,16,p.payload.size()+8);
    bytes.insert(bytes.end(),p.payload.begin(),p.payload.end());return checksum(bytes);
}
int main(){try{
    STransmitHarness d;std::mt19937_64 rng(0x768791feedULL);std::vector<Packet> packets;
    std::deque<unsigned> expected;std::vector<bool> completed(1800,false);unsigned wantDrops=0,wantGood=0;
    for(unsigned i=0;i<completed.size();++i){
        Packet p{};p.id=i;p.mode=i%12;p.src=rng();p.dst=rng();p.sport=rng();p.dport=1+rng()%65535;
        p.ident=rng();p.ttl=1+rng()%255;p.tos=rng();
        unsigned size=p.mode==1?0:(p.mode==7?600:(p.mode==9?2:(p.mode==10?512:1+rng()%480)));
        p.payload.resize(size);for(auto&b:p.payload)b=p.mode==11?255:rng();p.length=size;
        if(p.mode==2)++p.length;if(p.mode==3)--p.length;if(p.mode==6)p.ttl=0;if(p.mode==8)p.length=65535;
        if(p.mode==9){p.payload={0,0};put16(p.payload,0,udpChecksum(p));check(udpChecksum(p)==0,"TX zero checksum vector");}
        p.valid=p.mode==0||p.mode==1||p.mode==9||p.mode==10||p.mode==11;
        if(p.valid){
            p.wire.resize(28);p.wire[0]=0x45;p.wire[1]=p.tos;put16(p.wire,2,28+size);put16(p.wire,4,p.ident);
            put16(p.wire,6,0x4000);p.wire[8]=p.ttl;p.wire[9]=17;put32(p.wire,12,p.src);put32(p.wire,16,p.dst);
            std::vector<uint8_t> header(p.wire.begin(),p.wire.begin()+20);put16(p.wire,10,checksum(header));
            put16(p.wire,20,p.sport);put16(p.wire,22,p.dport);put16(p.wire,24,8+size);
            uint16_t c=udpChecksum(p);put16(p.wire,26,c?c:65535);
            p.wire.insert(p.wire.end(),p.payload.begin(),p.payload.end());expected.push_back(i);++wantGood;
        }else ++wantDrops;
        packets.push_back(p);
    }
    d.set_io$$valid(0);d.set_io$$commandValid(0);d.set_io$$outReady(0);d.set_reset(1);d.step();d.step();d.set_reset(0);
    unsigned index=0,input=0,output=0,count=0,cycles=0,idle=0,drops=0,good=0,stalls=0;
    bool commandSent=false,driving=false,commandDriving=false,held=false;
    uint64_t hLo=0,hHi=0;unsigned hKeep=0;bool hLast=false;
    while(index<packets.size()||!expected.empty()||drops<wantDrops){
        if(index<packets.size()){
            auto&p=packets[index];
            if(!commandSent&&!commandDriving&&rng()%4){
                commandDriving=true;d.set_io$$sourceIp(p.src);d.set_io$$destinationIp(p.dst);
                d.set_io$$sourcePort(p.sport);d.set_io$$destinationPort(p.dport);d.set_io$$length(p.length);
                d.set_io$$identification(p.ident);d.set_io$$ttl(p.ttl);d.set_io$$tos(p.tos);
            }
            if(commandSent&&!driving&&rng()%5){
                driving=true;count=std::min(16U,unsigned(p.payload.size())-input);uint64_t lo=rng(),hi=rng();
                for(unsigned i=0;i<count;++i){auto&w=i<8?lo:hi;unsigned sh=8*(i%8);
                    w=(w&~(uint64_t(255)<<sh))|(uint64_t(p.payload[input+i])<<sh);}
                unsigned keep=(1U<<count)-1;if(p.mode==4&&input==0)keep^=2;
                d.set_io$$lo(lo);d.set_io$$hi(hi);d.set_io$$keep(keep);d.set_io$$last(input+count==p.payload.size());
                d.set_io$$error(p.mode==5&&input+count==p.payload.size());
                // Command wires may change immediately after command acceptance.
                d.set_io$$sourceIp(rng());d.set_io$$destinationIp(rng());d.set_io$$length(rng());
            }
        }
        bool ready=rng()%5<3;d.set_io$$outReady(ready);d.set_io$$commandValid(commandDriving);d.set_io$$valid(driving);
        d.step();++cycles;bool progress=false;
        if(held)check(d.get_io$$outValid()&&d.get_io$$outLo()==hLo&&d.get_io$$outHi()==hHi&&
            d.get_io$$outKeep()==hKeep&&bool(d.get_io$$outLast())==hLast,"TX output unstable under backpressure");
        held=d.get_io$$outValid()&&!ready;if(held){++stalls;hLo=d.get_io$$outLo();hHi=d.get_io$$outHi();hKeep=d.get_io$$outKeep();hLast=d.get_io$$outLast();}
        if(d.get_io$$outValid()){
            check(!expected.empty(),"invalid TX packet escaped");auto&e=packets[expected.front()];
            check(completed[e.id],"TX started before complete payload validated");
            check(!d.get_io$$outError()&&d.get_io$$outDestination()==e.dst,"TX error or route metadata mismatch");
            if(ready){unsigned keep=d.get_io$$outKeep();check((keep&(keep+1))==0,"TX sparse output keep");
                unsigned n=__builtin_popcount(keep);check(output+n<=e.wire.size(),"extra TX data");
                for(unsigned i=0;i<n;++i){uint64_t w=i<8?d.get_io$$outLo():d.get_io$$outHi();
                    if(uint8_t(w>>(8*(i%8)))!=e.wire[output+i]){std::cerr<<"packet="<<e.id<<" byte="<<output+i<<'\n';
                        throw std::runtime_error("TX header/checksum/payload mismatch");}}
                output+=n;if(d.get_io$$outLast()){check(output==e.wire.size(),"truncated TX packet");expected.pop_front();output=0;++good;}
                else check(n==16,"partial nonfinal TX beat");progress=true;
            }
        }
        if(commandDriving&&d.get_io$$commandReady()){commandSent=true;commandDriving=false;progress=true;}
        if(driving&&d.get_io$$ready()){
            input+=count;driving=false;progress=true;
            if(input==packets[index].payload.size()){completed[index]=true;++index;input=0;commandSent=false;}
        }
        if(d.get_io$$dropped()){++drops;progress=true;}
        idle=progress?0:idle+1;
        if(idle>=2000){std::cerr<<"index="<<index<<" good="<<good<<" drops="<<drops<<'\n';throw std::runtime_error("TX deadlock");}
    }
    d.set_io$$valid(0);d.set_io$$commandValid(0);d.set_io$$outReady(1);
    for(unsigned i=0;i<20;++i){d.step();check(!d.get_io$$outValid()&&!d.get_io$$dropped(),"late TX duplicate");}
    check(good==wantGood&&drops==wantDrops,"TX packet accounting");
    std::cout<<"IPV4_UDP_TRANSMIT_PASS packets=1800 emitted="<<good<<" dropped="<<drops<<" cycles="<<cycles<<" stalls="<<stalls<<'\n';
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
