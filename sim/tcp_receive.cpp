#include "TcpReceiveHarness.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
static void check(bool ok,const char*s){if(!ok)throw std::runtime_error(s);}
static void put(std::vector<uint8_t>&v,unsigned p,unsigned n,uint64_t x){for(unsigned i=0;i<n;++i)v[p+i]=x>>(8*(n-i-1));}
static uint16_t checksum(std::vector<uint8_t>v){if(v.size()%2)v.push_back(0);uint32_t sum=0;
    for(unsigned i=0;i<v.size();i+=2)sum+=(v[i]<<8)|v[i+1];while(sum>>16)sum=(sum&65535)+(sum>>16);return uint16_t(~sum);}
static uint16_t tcpSum(uint32_t src,uint32_t dst,const std::vector<uint8_t>&tcp){
    std::vector<uint8_t>v(12);put(v,0,4,src);put(v,4,4,dst);v[9]=6;put(v,10,2,tcp.size());
    v.insert(v.end(),tcp.begin(),tcp.end());return checksum(v);
}
struct Packet{
    uint32_t src,dst,seq,ack,ts=0,tse=0;uint16_t sp,dp,window,urgent,mss=0;unsigned flags,hdr,optionFlags=0,scale=0,sacks=0,id;
    std::array<uint32_t,4>left{},right{};std::vector<uint8_t>payload;
};
struct Beat{uint64_t lo,hi;uint16_t keep;bool last,error;uint32_t src,dst;uint16_t len;unsigned id;};
int main(){try{
    STcpReceiveHarness d;std::mt19937_64 rng(0x929373232018ULL);
    std::deque<Beat>input;std::deque<Packet>expected;std::vector<bool>complete(2400,false);unsigned wantDrops=0,wantGood=0;
    for(unsigned p=0;p<complete.size();++p){
        unsigned mode=p%24,n=mode==10||mode==20?0:(mode==18?600:(mode==19?2:(p<128?p%17:rng()%401)));
        Packet e{};e.id=p;e.src=rng();e.dst=rng();e.sp=rng();e.dp=1+rng()%65535;e.seq=p%5?rng():0xfffffff0U;e.ack=rng();
        e.flags=(mode==1||mode==3||mode==7||mode==14)?2:0x18;e.window=rng();e.urgent=rng();
        e.payload.resize(n);for(auto&b:e.payload)b=rng();std::vector<uint8_t>opt;
        auto timestamp=[&](uint32_t a,uint32_t b){unsigned o=opt.size();opt.resize(o+10);opt[o]=8;opt[o+1]=10;put(opt,o+2,4,a);put(opt,o+6,4,b);};
        if(mode==1||mode==2){opt={2,4,5,180,1,3,3,7,4,2};e.ts=rng();e.tse=rng();timestamp(e.ts,e.tse);
            e.optionFlags=mode==1?15:8;if(mode==1){e.mss=1460;e.scale=7;}}
        if(mode==3){opt={3,3,255};e.optionFlags=2;e.scale=14;}
        if(mode==4){opt={30,6,11,22,33,44,1};e.ts=rng();e.tse=rng();timestamp(e.ts,e.tse);e.optionFlags=8;}
        if(mode==5){e.sacks=1+(p/24)%4;opt.resize(2+8*e.sacks);opt[0]=5;opt[1]=opt.size();
            for(unsigned i=0;i<e.sacks;++i){e.left[i]=rng();e.right[i]=e.left[i]+100;put(opt,2+8*i,4,e.left[i]);put(opt,6+8*i,4,e.right[i]);}}
        if(mode==6)opt={0,30,0,255,8,1,3,0}; // EOL ends parsing; trailing bytes are ignored.
        if(mode==7){opt={2,4,4,176,2,4,5,180};e.mss=1200;e.ts=rng();e.tse=rng();timestamp(e.ts,e.tse);timestamp(~e.ts,~e.tse);e.optionFlags=9;}
        if(mode==11)opt={30,0,0,0};if(mode==12)opt={30,1,0,0};if(mode==13)opt={30,255,0,0};
        if(mode==14)opt={2,3,1,0};if(mode==20)opt.assign(40,1);if(mode==21)opt={8,8,0,0,0,0,0,0};
        if(mode==22)opt={5,11,0,0,0,0,0,0,0,0,0};
        while(opt.size()%4)opt.push_back(0);e.hdr=20+opt.size();
        std::vector<uint8_t>tcp(20);put(tcp,0,2,e.sp);put(tcp,2,2,e.dp);put(tcp,4,4,e.seq);put(tcp,8,4,e.ack);
        tcp[12]=(e.hdr/4)<<4 | (rng()%16);tcp[13]=e.flags;put(tcp,14,2,e.window);put(tcp,18,2,e.urgent);
        if(mode==9)tcp[12]=0x40;if(mode==10)tcp[12]=0xf0;
        tcp.insert(tcp.end(),opt.begin(),opt.end());tcp.insert(tcp.end(),e.payload.begin(),e.payload.end());
        if(mode==19){put(tcp,tcp.size()-2,2,0);uint16_t c=tcpSum(e.src,e.dst,tcp);put(tcp,tcp.size()-2,2,c);
            put(e.payload,0,2,c);check(tcpSum(e.src,e.dst,tcp)==0,"TCP computed-zero vector");}
        put(tcp,16,2,tcpSum(e.src,e.dst,tcp));
        if(mode==15){put(tcp,16,2,0);if(tcpSum(e.src,e.dst,tcp)==0)tcp[8]^=1;}
        unsigned ipLength=tcp.size();if(mode==23)++ipLength;if(mode==8)tcp.pop_back();
        bool good=mode<=7||mode==19||mode==20;
        if(good){expected.push_back(e);++wantGood;}else ++wantDrops;
        for(unsigned offset=0;offset<tcp.size();offset+=16){unsigned count=std::min(16U,unsigned(tcp.size())-offset);
            Beat b{rng(),rng(),uint16_t((1U<<count)-1),offset+count==tcp.size(),mode==16&&offset+count==tcp.size(),e.src,e.dst,uint16_t(ipLength),p};
            for(unsigned i=0;i<count;++i){auto&w=i<8?b.lo:b.hi;unsigned sh=8*(i%8);w=(w&~(uint64_t(255)<<sh))|(uint64_t(tcp[offset+i])<<sh);}
            if(mode==17&&offset==0)b.keep^=2;input.push_back(b);
        }
    }
    d.set_io$$valid(0);d.set_io$$metaReady(0);d.set_io$$outReady(0);d.set_reset(1);d.step();d.step();d.set_reset(0);
    Packet current{};bool active=false,driving=false,held=false;uint64_t hLo=0,hHi=0;unsigned hKeep=0;bool hLast=false;
    unsigned payloadPos=0,cycles=0,drops=0,good=0,metas=0,idle=0;
    while(!input.empty()||!expected.empty()||active||drops<wantDrops){
        bool mr=rng()%5<2,pr=rng()%7<4;if(!driving&&!input.empty()&&rng()%4)driving=true;
        d.set_io$$valid(driving);d.set_io$$metaReady(mr);d.set_io$$outReady(pr);
        if(driving){auto b=input.front();d.set_io$$lo(b.lo);d.set_io$$hi(b.hi);d.set_io$$keep(b.keep);d.set_io$$last(b.last);d.set_io$$error(b.error);
            d.set_io$$sourceIp(b.src);d.set_io$$destinationIp(b.dst);d.set_io$$ipLength(b.len);}
        d.step();++cycles;bool progress=false;
        if(d.get_io$$metaValid()){
            check(!active&&!expected.empty(),"unexpected TCP metadata event");auto&e=expected.front();check(complete[e.id],"TCP exposed before segment end");
            check(d.get_io$$srcIp()==e.src&&d.get_io$$dstIp()==e.dst&&d.get_io$$srcPort()==e.sp&&d.get_io$$dstPort()==e.dp&&
                d.get_io$$sequence()==e.seq&&d.get_io$$acknowledgement()==e.ack&&d.get_io$$flags()==e.flags&&
                d.get_io$$window()==e.window&&d.get_io$$urgent()==e.urgent&&d.get_io$$headerLength()==e.hdr&&
                d.get_io$$payloadLength()==e.payload.size(),"TCP base header mismatch");
            if(d.get_io$$optionFlags()!=e.optionFlags||d.get_io$$mss()!=e.mss||d.get_io$$scale()!=e.scale||
                d.get_io$$timestamp()!=e.ts||d.get_io$$timestampEcho()!=e.tse||d.get_io$$sackCount()!=e.sacks){
                std::cerr<<"packet="<<e.id<<" flags="<<d.get_io$$optionFlags()<<" expected="<<e.optionFlags<<'\n';throw std::runtime_error("TCP option mismatch");}
            for(unsigned i=0;i<4;++i){uint64_t l=i<2?d.get_io$$sackLeftLo():d.get_io$$sackLeftHi();uint64_t r=i<2?d.get_io$$sackRightLo():d.get_io$$sackRightHi();
                check(uint32_t(l>>(32*(i%2)))==e.left[i]&&uint32_t(r>>(32*(i%2)))==e.right[i],"TCP SACK block mismatch");}
            if(mr){current=e;expected.pop_front();active=true;payloadPos=0;++metas;progress=true;}
        }
        if(held)check(d.get_io$$outValid()&&d.get_io$$outLo()==hLo&&d.get_io$$outHi()==hHi&&d.get_io$$outKeep()==hKeep&&bool(d.get_io$$outLast())==hLast,"TCP payload unstable");
        held=d.get_io$$outValid()&&!pr;if(held){hLo=d.get_io$$outLo();hHi=d.get_io$$outHi();hKeep=d.get_io$$outKeep();hLast=d.get_io$$outLast();}
        if(d.get_io$$outValid()){
            check(active&&!d.get_io$$outError(),"TCP payload before metadata or erroneous payload");
            if(pr){unsigned keep=d.get_io$$outKeep();unsigned count=__builtin_popcount(keep);
                check((keep&(keep+1))==0&&payloadPos+count<=current.payload.size(),"TCP payload keep/length");
                for(unsigned i=0;i<count;++i){uint64_t word=i<8?d.get_io$$outLo():d.get_io$$outHi();check(uint8_t(word>>(8*(i%8)))==current.payload[payloadPos+i],"TCP payload bytes");}
                payloadPos+=count;if(d.get_io$$outLast()){check(payloadPos==current.payload.size(),"TCP payload truncated");active=false;++good;}else check(count==16,"TCP short nonfinal beat");progress=true;}
        }
        if(driving&&d.get_io$$ready()){if(input.front().last)complete[input.front().id]=true;input.pop_front();driving=false;progress=true;}
        if(d.get_io$$dropped()){++drops;progress=true;}idle=progress?0:idle+1;
        if(idle>2000){std::cerr<<"good="<<good<<" drops="<<drops<<" pending="<<input.size()<<'\n';throw std::runtime_error("TCP receive deadlock");}
    }
    d.set_io$$valid(0);d.set_io$$metaReady(1);d.set_io$$outReady(1);
    for(unsigned i=0;i<30;++i){d.step();check(!d.get_io$$metaValid()&&!d.get_io$$outValid()&&!d.get_io$$dropped(),"TCP duplicate completion");}
    check(good==wantGood&&metas==good&&drops==wantDrops,"TCP receive accounting");
    std::cout<<"TCP_RECEIVE_PASS segments=2400 delivered="<<good<<" dropped="<<drops<<" cycles="<<cycles
        <<" mss_ws_ts_sack=pass malformed_options=pass zero_checksum=pass reserved_bits=ignored\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
