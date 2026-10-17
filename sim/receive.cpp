#include "ReceiveHarness.h"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void put16(std::vector<uint8_t>& v,unsigned p,uint16_t n){v[p]=n>>8;v[p+1]=n;}
static uint16_t sum(const std::vector<uint8_t>& v,uint32_t src,uint32_t dst){
    uint32_t s=(src>>16)+(src&65535)+(dst>>16)+(dst&65535)+17+v.size();
    for(unsigned i=0;i<v.size();++i)s+=uint32_t(v[i])<<((i&1)?0:8);
    while(s>>16)s=(s&65535)+(s>>16);
    return uint16_t(~s);
}
struct Beat{uint64_t lo,hi;uint16_t keep;bool last,error;uint32_t src,dst;uint16_t len;unsigned id;};
static bool same(const Beat&a,const Beat&b){return a.lo==b.lo&&a.hi==b.hi&&a.keep==b.keep&&a.last==b.last&&a.src==b.src&&a.dst==b.dst;}
int main(){try{
    SReceiveHarness d;
    std::mt19937_64 rng(0x768baffULL);
    std::deque<Beat> inputs,expected;
    std::vector<bool> completed(1800,false);
    unsigned expectedDrops=0,good=0;
    for(unsigned p=0;p<completed.size();++p){
        unsigned mode=p%9;
        unsigned length=mode==5?300+rng()%100:8+rng()%249;
        uint32_t src=rng(),dst=rng();
        std::vector<uint8_t> wire(length);
        put16(wire,0,rng());put16(wire,2,1+rng()%65535);put16(wire,4,length);
        for(unsigned i=8;i<length;++i)wire[i]=rng();
        uint16_t c=sum(wire,src,dst);put16(wire,6,c?c:65535);
        if(mode==1)put16(wire,6,0);
        if(mode==2)wire[0]^=1;
        if(mode==3)wire[5]^=1;
        if(mode==4)src^=1;
        if(mode==7)wire.resize(4);
        bool accept=mode==0||mode==1||mode==8;
        if(accept)++good;else ++expectedDrops;
        for(unsigned offset=0;offset<wire.size();offset+=16){
            unsigned count=std::min(16U,unsigned(wire.size())-offset);
            Beat b{rng(),rng(),uint16_t((1U<<count)-1),offset+count==wire.size(),mode==6,src,dst,uint16_t(length),p};
            for(unsigned i=0;i<count;++i){auto& word=i<8?b.lo:b.hi;unsigned shift=8*(i%8);
                word=(word&~(uint64_t(255)<<shift))|(uint64_t(wire[offset+i])<<shift);}
            inputs.push_back(b);
            if(accept)expected.push_back(b);
        }
    }
    d.set_io$$valid(0);d.set_io$$outReady(0);d.set_reset(1);d.step();d.step();d.set_reset(0);
    bool driving=false,held=false;Beat heldBeat{};
    unsigned cycles=0,outputPackets=0,drops=0,idle=0,stallCycles=0;
    while(!inputs.empty()||!expected.empty()||drops<expectedDrops){
        bool outReady=(rng()%7)<3;
        if(!driving&&!inputs.empty()&&rng()%4)driving=true;
        d.set_io$$valid(driving);d.set_io$$outReady(outReady);
        if(driving){auto b=inputs.front();d.set_io$$lo(b.lo);d.set_io$$hi(b.hi);d.set_io$$keep(b.keep);
            d.set_io$$last(b.last);d.set_io$$error(b.error);d.set_io$$sourceIp(b.src);
            d.set_io$$destinationIp(b.dst);d.set_io$$ipLength(b.len);}
        d.step();++cycles;
        Beat out{d.get_io$$outLo(),d.get_io$$outHi(),uint16_t(d.get_io$$outKeep()),bool(d.get_io$$outLast()),false,
                 uint32_t(d.get_io$$outSourceIp()),uint32_t(d.get_io$$outDestinationIp()),0,0};
        if(held)check(d.get_io$$outValid()&&same(out,heldBeat),"validated UDP changed under output stall");
        held=d.get_io$$outValid()&&!outReady;if(held){heldBeat=out;++stallCycles;}
        bool progress=false;
        if(d.get_io$$outValid()&&outReady){
            check(!expected.empty(),"bad or duplicate UDP escaped");
            check(completed[expected.front().id],"UDP exposed before packet end/checksum");
            check(same(out,expected.front()),"validated UDP byte/context/order mismatch");
            if(out.last)++outputPackets;expected.pop_front();progress=true;
        }
        if(driving&&d.get_io$$ready()){
            if(inputs.front().last)completed[inputs.front().id]=true;
            inputs.pop_front();driving=false;progress=true;
        }
        if(d.get_io$$dropped()){++drops;progress=true;}
        idle=progress?0:idle+1;check(idle<1000,"UDP receive deadlock");
    }
    // Detect late duplication, including an already-issued RAM response.
    d.set_io$$valid(0);d.set_io$$outReady(1);
    for(unsigned i=0;i<20;++i){d.step();check(!d.get_io$$outValid()&&!d.get_io$$dropped(),"late duplicate UDP result");}
    check(outputPackets==good&&drops==expectedDrops,"UDP receive packet accounting");
    std::cout<<"UDP_RECEIVE_PASS packets="<<completed.size()<<" delivered="<<outputPackets<<" dropped="<<drops
             <<" cycles="<<cycles<<" stalled_outputs="<<stallCycles<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
