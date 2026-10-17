#include "SliceHarness.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
static void check(bool ok,const char* s){if(!ok)throw std::runtime_error(s);}
int main(){try{
    SSliceHarness d;std::mt19937_64 rng(0x51ce20261017ULL);
    d.set_io$$valid(0);d.set_io$$configValid(0);d.set_io$$outReady(0);
    d.set_reset(1);d.step();d.step();d.set_reset(0);
    unsigned cycles=0,stalls=0,truncated=0;
    for(unsigned p=0;p<2500;++p){
        unsigned skip=p%128,length=p<256?p%33:rng()%1501,padding=rng()%65;
        unsigned size=skip+length+padding;
        if(p%7==0)size= size?rng()%size:0;
        std::vector<uint8_t> wire(size);for(auto&b:wire)b=rng();
        unsigned wanted=std::min(length,size>skip?size-skip:0);
        bool shortPacket=size<skip+length,macError=p%13==0;
        if(shortPacket)++truncated;
        uint64_t context=rng();
        d.set_io$$configValid(1);d.set_io$$skip(skip);d.set_io$$length(length);d.set_io$$context(context);
        unsigned timeout=0;
        do{d.step();check(++timeout<100,"slice config deadlock");}while(!d.get_io$$configReady());
        d.set_io$$configValid(0);
        bool driving=false,inDone=false,outDone=false,held=false;
        uint64_t heldLo=0,heldHi=0;unsigned heldKeep=0;bool heldLast=false,heldError=false;
        unsigned input=0,output=0,count=0;
        while(!inDone||!outDone){
            if(!driving&&!inDone&&rng()%4){
                driving=true;count=std::min(16U,size-input);
                uint64_t lo=rng(),hi=rng();
                for(unsigned i=0;i<count;++i){auto&w=i<8?lo:hi;unsigned sh=8*(i%8);
                    w=(w&~(uint64_t(255)<<sh))|(uint64_t(wire[input+i])<<sh);}
                d.set_io$$lo(lo);d.set_io$$hi(hi);d.set_io$$keep((1U<<count)-1);
                d.set_io$$last(input+count==size);d.set_io$$error(macError&&input+count==size);
            }
            bool ready=rng()%5<2;d.set_io$$valid(driving);d.set_io$$outReady(ready);d.step();++cycles;
            if(held)check(d.get_io$$outValid()&&d.get_io$$outLo()==heldLo&&d.get_io$$outHi()==heldHi&&
                d.get_io$$outKeep()==heldKeep&&bool(d.get_io$$outLast())==heldLast&&
                bool(d.get_io$$outError())==heldError,"slice output unstable under backpressure");
            held=d.get_io$$outValid()&&!ready;
            if(held){++stalls;heldLo=d.get_io$$outLo();heldHi=d.get_io$$outHi();heldKeep=d.get_io$$outKeep();
                heldLast=d.get_io$$outLast();heldError=d.get_io$$outError();}
            if(d.get_io$$outValid()&&ready){
                check(!outDone,"slice duplicate end");check(d.get_io$$outContext()==context,"slice context mismatch");
                unsigned keep=d.get_io$$outKeep();check((keep&(keep+1))==0,"slice sparse output keep");
                unsigned n=__builtin_popcount(keep);
                check(output+n<=wanted,"slice padding or extra bytes escaped");
                for(unsigned i=0;i<n;++i){uint64_t w=i<8?d.get_io$$outLo():d.get_io$$outHi();
                    check(uint8_t(w>>(8*(i%8)))==wire[skip+output+i],"slice byte alignment mismatch");}
                output+=n;
                if(d.get_io$$outLast()){
                    check(inDone,"slice final beat before input end/padding");
                    check(output==wanted,"slice missing payload bytes");
                    check(bool(d.get_io$$outError())==(shortPacket||macError),"slice final error mismatch");outDone=true;
                }else check(n==16,"slice nonfinal partial beat");
            }
            if(driving&&d.get_io$$ready()){input+=count;inDone=input==size;driving=false;}
            check(++timeout<20000,"slice data deadlock");
        }
        d.set_io$$valid(0);
    }
    std::cout<<"PACKET_SLICE_PASS packets=2500 truncated="<<truncated<<" cycles="<<cycles<<" stalls="<<stalls<<'\n';
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
