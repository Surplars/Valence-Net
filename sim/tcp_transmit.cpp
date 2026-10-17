#include "TcpTransmitHarness.h"
#include <algorithm>
#include <array>
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
    std::array<uint8_t,40> options;
    uint32_t src,dst,seq,ack;
    uint16_t sport,dport,length,ident,window,urgent;
    uint8_t ttl,tos,flags,optlen;
    unsigned id,mode;bool valid;
};
static std::vector<uint8_t> tcp(const Packet&p){
    std::vector<uint8_t> b(20+p.optlen);
    put16(b,0,p.sport);put16(b,2,p.dport);put32(b,4,p.seq);put32(b,8,p.ack);
    b[12]=(5+p.optlen/4)<<4;b[13]=p.flags;put16(b,14,p.window);put16(b,18,p.urgent);
    std::copy(p.options.begin(),p.options.begin()+p.optlen,b.begin()+20);
    b.insert(b.end(),p.payload.begin(),p.payload.end());return b;
}
static uint16_t tcpChecksum(const Packet&p){
    std::vector<uint8_t> b(12);put32(b,0,p.src);put32(b,4,p.dst);b[9]=6;
    auto t=tcp(p);put16(b,10,t.size());b.insert(b.end(),t.begin(),t.end());return checksum(b);
}
static void driveCommand(STcpTransmitHarness&d,const Packet&p){
    d.set_io$$sourceIp(p.src);d.set_io$$destinationIp(p.dst);
    d.set_io$$sourcePort(p.sport);d.set_io$$destinationPort(p.dport);
    d.set_io$$sequence(p.seq);d.set_io$$acknowledgement(p.ack);
    d.set_io$$flags(p.flags);d.set_io$$window(p.window);d.set_io$$urgent(p.urgent);
    d.set_io$$length(p.length);d.set_io$$identification(p.ident);d.set_io$$ttl(p.ttl);d.set_io$$tos(p.tos);
    d.set_io$$optionLength(p.optlen);
    uint64_t words[5]={};for(unsigned i=0;i<40;++i)words[i/8]|=uint64_t(p.options[i])<<(8*(i%8));
    d.set_io$$options0(words[0]);d.set_io$$options1(words[1]);d.set_io$$options2(words[2]);
    d.set_io$$options3(words[3]);d.set_io$$options4(words[4]);
}
int main(){try{
    STcpTransmitHarness d;std::mt19937_64 rng(0x92937323feedULL);std::vector<Packet> packets;
    std::deque<unsigned> expected;std::vector<bool> completed(2600,false);unsigned wantDrops=0,wantGood=0;
    std::array<unsigned,11> optionCoverage{};
    for(unsigned i=0;i<completed.size();++i){
        Packet p{};p.id=i;p.mode=i%26;p.src=rng();p.dst=rng();p.sport=rng();p.dport=1+rng()%65535;
        p.seq=i%2?uint32_t(0xfffffff0U+i):uint32_t(rng());p.ack=rng();p.flags=rng();p.window=rng();p.urgent=rng();
        p.ident=rng();p.ttl=1+rng()%255;p.tos=rng();
        p.optlen=4*(p.mode<=10?p.mode:rng()%11);
        for(auto&b:p.options)b=rng(); // Nonzero unused option bytes must never escape or affect checksum.
        for(unsigned j=0;j<p.optlen;++j)p.options[j]=1;
        if(p.optlen>=4){p.options[0]=2;p.options[1]=4;p.options[2]=5;p.options[3]=180;}
        if(p.optlen>=16){p.options[4]=8;p.options[5]=10;for(unsigned j=6;j<14;++j)p.options[j]=rng();}
        unsigned size=p.mode==11?0:(p.mode==19?600:(p.mode==12?2:(i%7==0?512:17+rng()%480)));
        if(p.mode<=10&&i%3==0)size=(i/26)%18;
        p.payload.resize(size);for(auto&b:p.payload)b=p.mode==23?255:rng();p.length=size;
        if(p.mode==13||p.mode==25)p.length=65535;
        if(p.mode==14)++p.length;if(p.mode==15)--p.length;
        if(p.mode==18)p.ttl=0;if(p.mode==20)p.optlen=44;
        if(p.mode==21)p.optlen=4*(rng()%10)+1+rng()%3;
        if(p.mode==24)p.length=0;
        if(p.mode==12){p.payload={0,0};put16(p.payload,0,tcpChecksum(p));check(tcpChecksum(p)==0,"TCP TX zero checksum vector");}
        p.valid=p.mode<=12||p.mode==23;
        if(p.valid){
            ++optionCoverage[p.optlen/4];
            p.wire.resize(20);p.wire[0]=0x45;p.wire[1]=p.tos;put16(p.wire,2,40+p.optlen+size);
            put16(p.wire,4,p.ident);put16(p.wire,6,0x4000);p.wire[8]=p.ttl;p.wire[9]=6;
            put32(p.wire,12,p.src);put32(p.wire,16,p.dst);put16(p.wire,10,checksum(p.wire));
            auto t=tcp(p);put16(t,16,tcpChecksum(p));p.wire.insert(p.wire.end(),t.begin(),t.end());
            expected.push_back(i);++wantGood;
        }else ++wantDrops;
        packets.push_back(p);
    }
    d.set_io$$valid(0);d.set_io$$commandValid(0);d.set_io$$outReady(0);d.set_reset(1);d.step();d.step();d.set_reset(0);
    // Cancel partial input, an offered header and an already started output.
    for(unsigned scenario=0;scenario<3;++scenario){
        Packet p=packets[10];p.length=32;p.payload.resize(32);
        driveCommand(d,p);d.set_io$$commandValid(1);d.step();check(d.get_io$$commandReady(),"reset setup command");
        d.set_io$$commandValid(0);d.set_io$$lo(0);d.set_io$$hi(0);d.set_io$$keep(65535);d.set_io$$error(0);
        unsigned beats=0,outs=0,steps=0;
        while(true){
            d.set_io$$valid(beats<(scenario==0?1U:2U));d.set_io$$last(beats==1);
            d.set_io$$outReady(scenario==2);d.step();++steps;
            if(d.get_io$$ready()&&beats<(scenario==0?1U:2U))++beats;
            if(d.get_io$$outValid()&&scenario==2)++outs;
            if((scenario==0&&beats==1)||(scenario==1&&d.get_io$$outValid())||(scenario==2&&outs==2))break;
            check(steps<100,"reset setup timeout");
        }
        d.set_io$$valid(0);d.set_reset(1);d.step();d.step();d.set_reset(0);d.set_io$$outReady(1);
        for(unsigned j=0;j<15;++j){d.step();check(!d.get_io$$outValid()&&!d.get_io$$dropped(),"stale TCP output after reset");}
    }
    unsigned index=0,input=0,output=0,count=0,cycles=0,idle=0,drops=0,good=0,stalls=0;
    bool commandSent=false,driving=false,commandDriving=false,held=false;
    uint64_t hLo=0,hHi=0;unsigned hKeep=0,hDest=0;bool hLast=false,hError=false;
    while(index<packets.size()||!expected.empty()||drops<wantDrops){
        if(index<packets.size()){
            auto&p=packets[index];
            if(!commandSent&&!commandDriving&&rng()%4){commandDriving=true;driveCommand(d,p);}
            if(commandSent&&!driving&&rng()%5){
                driving=true;count=std::min(16U,unsigned(p.payload.size())-input);uint64_t lo=rng(),hi=rng();
                for(unsigned i=0;i<count;++i){auto&w=i<8?lo:hi;unsigned sh=8*(i%8);
                    w=(w&~(uint64_t(255)<<sh))|(uint64_t(p.payload[input+i])<<sh);}
                unsigned keep=(1U<<count)-1;if(p.mode==16&&input==0)keep^=2;
                if(p.mode==22&&input==0)keep=32767;
                d.set_io$$lo(lo);d.set_io$$hi(hi);d.set_io$$keep(keep);d.set_io$$last(input+count==p.payload.size());
                d.set_io$$error(p.mode==17&&input+count==p.payload.size());
                d.set_io$$sourceIp(rng());d.set_io$$destinationIp(rng());d.set_io$$length(rng());
                d.set_io$$optionLength(rng()%64);d.set_io$$sequence(rng());d.set_io$$options0(rng());
            }
        }
        bool ready=cycles%347>49&&rng()%5<3;
        d.set_io$$outReady(ready);d.set_io$$commandValid(commandDriving);d.set_io$$valid(driving);
        d.step();++cycles;bool progress=false;
        if(held)check(d.get_io$$outValid()&&d.get_io$$outLo()==hLo&&d.get_io$$outHi()==hHi&&
            d.get_io$$outKeep()==hKeep&&bool(d.get_io$$outLast())==hLast&&bool(d.get_io$$outError())==hError&&
            d.get_io$$outDestination()==hDest,"TCP TX output unstable under backpressure");
        held=d.get_io$$outValid()&&!ready;
        if(held){++stalls;hLo=d.get_io$$outLo();hHi=d.get_io$$outHi();hKeep=d.get_io$$outKeep();
            hLast=d.get_io$$outLast();hError=d.get_io$$outError();hDest=d.get_io$$outDestination();}
        if(d.get_io$$outValid()){
            check(!expected.empty(),"invalid TCP TX packet escaped");auto&e=packets[expected.front()];
            check(completed[e.id],"TCP TX started before payload validation");
            check(!d.get_io$$outError()&&d.get_io$$outDestination()==e.dst,"TCP TX error or route metadata mismatch");
            if(ready){unsigned keep=d.get_io$$outKeep();check((keep&(keep+1))==0,"TCP TX sparse output keep");
                unsigned n=__builtin_popcount(keep);check(output+n<=e.wire.size(),"extra TCP TX data");
                for(unsigned i=0;i<n;++i){uint64_t w=i<8?d.get_io$$outLo():d.get_io$$outHi();
                    if(uint8_t(w>>(8*(i%8)))!=e.wire[output+i]){std::cerr<<"packet="<<e.id<<" byte="<<output+i
                        <<" got="<<unsigned(uint8_t(w>>(8*(i%8))))<<" want="<<unsigned(e.wire[output+i])<<'\n';
                        throw std::runtime_error("TCP TX header/checksum/payload mismatch");}}
                output+=n;if(d.get_io$$outLast()){check(output==e.wire.size(),"truncated TCP TX packet");expected.pop_front();output=0;++good;}
                else check(n==16,"partial nonfinal TCP TX beat");progress=true;
            }
        }
        if(commandDriving&&d.get_io$$commandReady()){commandSent=true;commandDriving=false;progress=true;}
        if(driving&&d.get_io$$ready()){
            input+=count;driving=false;progress=true;
            if(input==packets[index].payload.size()){completed[index]=true;++index;input=0;commandSent=false;}
        }
        if(d.get_io$$dropped()){++drops;progress=true;}
        idle=progress?0:idle+1;
        if(idle>=2000){std::cerr<<"index="<<index<<" good="<<good<<" drops="<<drops<<'\n';throw std::runtime_error("TCP TX deadlock");}
    }
    d.set_io$$valid(0);d.set_io$$commandValid(0);d.set_io$$outReady(1);
    for(unsigned i=0;i<20;++i){d.step();check(!d.get_io$$outValid()&&!d.get_io$$dropped(),"late TCP TX duplicate");}
    check(good==wantGood&&drops==wantDrops,"TCP TX packet accounting");
    for(auto n:optionCoverage)check(n>=100,"option length coverage");
    std::cout<<"IPV4_TCP_TRANSMIT_PASS packets=2600 emitted="<<good<<" dropped="<<drops<<" cycles="<<cycles
        <<" stalls="<<stalls<<" header_lengths=20..60 zero_checksum=pass reset=pass\n";
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
