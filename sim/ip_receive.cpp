#include "IpReceiveHarness.h"
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
static uint16_t checksum(std::vector<uint8_t> v){
    if(v.size()&1)v.push_back(0);uint32_t s=0;
    for(unsigned i=0;i<v.size();i+=2)s+=(v[i]<<8)|v[i+1];
    while(s>>16)s=(s&65535)+(s>>16);return uint16_t(~s);
}
struct Beat{uint64_t lo,hi;uint16_t keep;bool last,error,broadcast,first;unsigned id;};
struct Packet{std::vector<uint8_t> payload;uint32_t src,dst;uint16_t sport,dport;unsigned id;};
int main(){try{
    SIpReceiveHarness d;std::mt19937_64 rng(0x79176820261017ULL);
    std::deque<Beat> input;std::deque<Packet> expected;std::vector<bool> complete(2400,false);
    unsigned wantIp=0,wantUdp=0,wantOther=0,wantGood=0;
    constexpr uint32_t local=0xc0000202;
    for(unsigned p=0;p<complete.size();++p){
        unsigned mode=p%24,payloadLength=mode==22?0:(mode==23?600:rng()%401);
        unsigned ihl=mode==12?(p/24)%15:5;if(mode==12&&ihl>=5)++ihl;
        unsigned headerLength=std::max(20U,ihl*4);
        uint32_t src=0xc6336401+p,dst=mode==11?local+1:((mode==14||mode==15)?0xffffffff:local);
        uint16_t sport=rng(),dport=1+rng()%65535;
        std::vector<uint8_t> payload(payloadLength);for(auto&b:payload)b=rng();
        std::vector<uint8_t> udp(8);put16(udp,0,sport);put16(udp,2,dport);put16(udp,4,payloadLength+8);
        udp.insert(udp.end(),payload.begin(),payload.end());
        std::vector<uint8_t> pseudo(12);put32(pseudo,0,src);put32(pseudo,4,dst);pseudo[9]=17;put16(pseudo,10,udp.size());
        pseudo.insert(pseudo.end(),udp.begin(),udp.end());uint16_t c=checksum(pseudo);put16(udp,6,c?c:65535);
        if(mode==1)put16(udp,6,0);
        if(mode==2)udp[0]^=1;
        if(mode==20)put16(udp,4,udp.size()+1);
        std::vector<uint8_t> wire(headerLength,0);
        wire[0]=0x40+ihl;wire[1]=rng();put16(wire,2,headerLength+udp.size());put16(wire,4,rng());
        put16(wire,6,0x4000);wire[8]=64;wire[9]=mode==13?6:17;put32(wire,12,src);put32(wire,16,dst);
        if(mode==5)put16(wire,2,19);
        if(mode==6)wire[8]=(p/24)&1; // RFC 1122 destination hosts accept TTL 0 and 1.
        if(mode==7)wire[0]=0x65;
        if(mode==8)put16(wire,6,0x8000);
        if(mode==9)put16(wire,6,0x2000);
        if(mode==10)put16(wire,6,1);
        if(mode==12&&headerLength>20){wire[20]=1;wire[21]=1;}
        unsigned padding=mode==21?31+rng()%35:rng()%17;
        if(mode==4)put16(wire,2,headerLength+udp.size()+padding+1);
        put16(wire,10,checksum(wire));if(mode==3)wire[10]^=1;
        wire.insert(wire.end(),udp.begin(),udp.end());
        for(unsigned i=0;i<padding;++i)wire.push_back(rng());
        if(mode==19)wire.resize(16);
        bool good=mode==0||mode==1||mode==6||mode==15||mode==21||mode==22;
        if(good){expected.push_back({payload,src,dst,sport,dport,p});++wantGood;}
        else if(mode==2||mode==20)++wantUdp;
        else if(mode==13)++wantOther;
        else ++wantIp;
        for(unsigned offset=0;offset<wire.size();offset+=16){
            unsigned n=std::min(16U,unsigned(wire.size())-offset);
            bool last=offset+n==wire.size()&&mode!=18;
            Beat b{rng(),rng(),uint16_t((1U<<n)-1),last,mode==16&&last,mode==15,offset==0,p};
            for(unsigned i=0;i<n;++i){auto&w=i<8?b.lo:b.hi;unsigned shift=8*(i%8);
                w=(w&~(uint64_t(255)<<shift))|(uint64_t(wire[offset+i])<<shift);}
            if(mode==17&&offset==0)b.keep^=2;
            input.push_back(b);
        }
        if(mode==18)input.push_back({rng(),rng(),0,true,false,false,false,p});
    }
    d.set_io$$valid(0);d.set_io$$outReady(0);d.set_reset(1);d.step();d.step();d.set_reset(0);
    bool driving=false,held=false;uint64_t holdLo=0,holdHi=0;unsigned holdKeep=0;bool holdLast=false;
    unsigned cycles=0,idle=0,ip=0,udp=0,other=0,good=0,offset=0,stalls=0;
    while(!input.empty()||!expected.empty()||ip+udp+other<wantIp+wantUdp+wantOther){
        if(!driving&&!input.empty()&&rng()%5)driving=true;
        bool ready=rng()%7<4;d.set_io$$valid(driving);d.set_io$$outReady(ready);
        if(driving){auto b=input.front();d.set_io$$lo(b.lo);d.set_io$$hi(b.hi);d.set_io$$keep(b.keep);
            d.set_io$$last(b.last);d.set_io$$error(b.error);
            d.set_io$$localIp(b.first?local:~local);d.set_io$$broadcast(b.first?b.broadcast:!b.broadcast);}
        d.step();++cycles;bool progress=false;
        if(held)check(d.get_io$$outValid()&&d.get_io$$outLo()==holdLo&&d.get_io$$outHi()==holdHi&&
            d.get_io$$outKeep()==holdKeep&&bool(d.get_io$$outLast())==holdLast,"IPv4 UDP output unstable");
        held=d.get_io$$outValid()&&!ready;
        if(held){++stalls;holdLo=d.get_io$$outLo();holdHi=d.get_io$$outHi();holdKeep=d.get_io$$outKeep();holdLast=d.get_io$$outLast();}
        if(d.get_io$$outValid()){
            check(!expected.empty(),"invalid IP/UDP payload escaped");auto&e=expected.front();
            check(complete[e.id],"payload escaped before frame validated");
            check(d.get_io$$sourceIp()==e.src&&d.get_io$$destinationIp()==e.dst&&
                d.get_io$$sourcePort()==e.sport&&d.get_io$$destinationPort()==e.dport&&
                d.get_io$$length()==e.payload.size(),"IPv4 UDP metadata/order mismatch");
            check(!d.get_io$$outError(),"bad frame reached committed output");
            if(ready){
                unsigned mask=d.get_io$$outKeep();check((mask&(mask+1))==0,"IPv4 UDP sparse output keep");
                unsigned n=__builtin_popcount(mask);check(offset+n<=e.payload.size(),"IPv4/UDP headers or padding escaped");
                for(unsigned i=0;i<n;++i){uint64_t w=i<8?d.get_io$$outLo():d.get_io$$outHi();
                    check(uint8_t(w>>(8*(i%8)))==e.payload[offset+i],"IPv4 UDP payload mismatch");}
                offset+=n;
                if(d.get_io$$outLast()){check(offset==e.payload.size(),"IPv4 UDP truncated output");expected.pop_front();offset=0;++good;}
                else check(n==16,"IPv4 UDP short nonfinal output");
                progress=true;
            }
        }
        if(driving&&d.get_io$$ready()){if(input.front().last)complete[input.front().id]=true;
            input.pop_front();driving=false;progress=true;}
        if(d.get_io$$ipDrops()){++ip;progress=true;}if(d.get_io$$udpDrops()){++udp;progress=true;}
        if(d.get_io$$other()){++other;progress=true;}
        idle=progress?0:idle+1;
        if(idle>=2000){std::cerr<<"ip="<<ip<<" udp="<<udp<<" other="<<other<<" good="<<good
            <<" input="<<input.size()<<" expected="<<expected.size()<<'\n';throw std::runtime_error("IPv4 UDP deadlock");}
    }
    d.set_io$$valid(0);d.set_io$$outReady(1);
    for(unsigned i=0;i<30;++i){d.step();check(!d.get_io$$outValid()&&!d.get_io$$ipDrops()&&!d.get_io$$udpDrops()&&!d.get_io$$other(),"late IPv4 duplicate");}
    check(ip==wantIp&&udp==wantUdp&&other==wantOther&&good==wantGood,"IPv4 UDP packet accounting");
    std::cout<<"IPV4_UDP_RECEIVE_PASS packets=2400 delivered="<<good<<" ip_drops="<<ip<<" udp_drops="<<udp
        <<" other_protocol="<<other<<" cycles="<<cycles<<" stalls="<<stalls<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
