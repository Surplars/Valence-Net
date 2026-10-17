#include "UdpHarness.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

static void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
static void put16(std::vector<uint8_t>& v,unsigned pos,uint16_t n) {v[pos]=n>>8;v[pos+1]=n;}
static uint16_t get16(const std::vector<uint8_t>& v,unsigned pos) {return (v[pos]<<8)|v[pos+1];}
static void put32(std::vector<uint8_t>& v,unsigned pos,uint32_t n) {put16(v,pos,n>>16);put16(v,pos+2,n);}
static uint16_t checksum(std::vector<uint8_t> data) {
    if(data.size()%2) data.push_back(0);
    uint32_t sum=0;
    for(unsigned i=0;i<data.size();i+=2) sum+=get16(data,i);
    while(sum>>16) sum=(sum&65535)+(sum>>16);
    return uint16_t(~sum);
}
static uint16_t udpSum(uint32_t src,uint32_t dst,uint16_t length,const std::vector<uint8_t>& data) {
    std::vector<uint8_t> bytes(12);
    put32(bytes,0,src);put32(bytes,4,dst);bytes[9]=17;put16(bytes,10,length);
    bytes.insert(bytes.end(),data.begin(),data.end());
    return checksum(bytes);
}

int main() {try {
    SUdpHarness d;
    std::mt19937_64 rng(0x76820261017ULL);
    unsigned passed=0,rejected=0,cycles=0,heldCycles=0;
    d.set_io$$valid(0);d.set_io$$outReady(0);
    d.set_reset(1);d.step();d.step();d.set_reset(0);
    for(unsigned p=0;p<2600;++p) {
        const unsigned mode=p%13;
        uint32_t src=rng(),dst=rng();
        unsigned payload=p<160?p%33:rng()%9001;
        std::vector<uint8_t> wire(payload+8);
        put16(wire,0,rng());put16(wire,2,1+rng()%65535);put16(wire,4,wire.size());
        for(unsigned i=8;i<wire.size();++i) wire[i]=rng();
        if(mode==5) put16(wire,4,wire.size()+1); // self-consistent checksum, bad UDP length
        uint16_t ipLength=wire.size();
        uint16_t c=udpSum(src,dst,ipLength,wire);
        put16(wire,6,c?c:65535);
        if(mode==1) put16(wire,6,0); // legal IPv4 UDP checksum omission
        if(mode==2) wire.back()^=1;
        if(mode==3) wire.pop_back();
        if(mode==4) wire.push_back(0);
        if(mode==6) ++ipLength;
        if(mode==9) src^=1;
        bool macError=mode==7;
        bool sparse=mode==8;
        bool partial=mode==10;
        if(mode==11) wire.resize(4); // cannot even contain the complete UDP header
        if(mode==12) { // zero checksum generation must appear on wire as 0xffff
            wire.assign(10,0);put16(wire,0,1234);put16(wire,2,5678);put16(wire,4,10);ipLength=10;
            put16(wire,8,udpSum(src,dst,ipLength,wire));
            check(udpSum(src,dst,ipLength,wire)==0,"zero checksum vector construction");
            put16(wire,6,65535);
        }
        const auto declared=wire.size()>=6?get16(wire,4):0;
        const bool hasChecksum=wire.size()>=8 && get16(wire,6)!=0;
        unsigned expectedErrors=macError?1:0;
        if(wire.size()<8 || declared<8 || declared!=ipLength || declared!=wire.size()) expectedErrors|=2;
        if(hasChecksum && udpSum(src,dst,ipLength,wire)!=0) expectedErrors|=4;
        if(sparse || partial) expectedErrors|=1;
        unsigned offset=0;
        bool done=false,seen=false;
        unsigned timeout=0;
        bool held=false;
        unsigned oldErrors=0,oldSrc=0,oldDst=0,oldLen=0;
        bool driving=false;
        unsigned count=0;
        uint16_t keep=0;
        while(!seen) {
            bool outReady=(rng()%5)==0;
            if(!driving && !done && rng()%4) {
                driving=true;
                count=std::min(16U,unsigned(wire.size())-offset);
                if(partial && offset==0) count=8;
                keep=uint16_t((1U<<count)-1);
                if(sparse && offset==0) keep^=2;
                uint64_t lo=rng(),hi=rng();
                for(unsigned i=0;i<count;++i) {
                    auto& word=i<8?lo:hi;
                    unsigned shift=(i%8)*8;
                    word=(word&~(uint64_t(255)<<shift))|(uint64_t(wire[offset+i])<<shift);
                }
                d.set_io$$lo(lo);d.set_io$$hi(hi);d.set_io$$keep(keep);
                d.set_io$$last(offset+count==wire.size());d.set_io$$error(macError&&offset==0);
                // Metadata must be sampled only at the first accepted beat.
                d.set_io$$sourceIp(offset==0?src:rng());
                d.set_io$$destinationIp(offset==0?dst:rng());
                d.set_io$$ipLength(offset==0?ipLength:uint16_t(rng()));
            }
            d.set_io$$valid(driving);d.set_io$$outReady(outReady);d.step();++cycles;
            if(held) check(d.get_io$$outValid() && d.get_io$$errors()==oldErrors &&
                d.get_io$$sourcePort()==oldSrc && d.get_io$$destinationPort()==oldDst &&
                d.get_io$$payloadLength()==oldLen,"UDP verdict changed under backpressure");
            held=d.get_io$$outValid()&&!outReady;
            if(held) {++heldCycles;oldErrors=d.get_io$$errors();oldSrc=d.get_io$$sourcePort();
                oldDst=d.get_io$$destinationPort();oldLen=d.get_io$$payloadLength();}
            if(d.get_io$$outValid()&&outReady) {
                check(done,"UDP verdict before input packet completed");
                if(!sparse && !partial && mode!=11) {
                    if(d.get_io$$errors()!=expectedErrors) {
                        std::cerr<<"packet="<<p<<" mode="<<mode<<" got="<<d.get_io$$errors()
                                 <<" expected="<<expectedErrors<<'\n';
                        throw std::runtime_error("UDP status mismatch");
                    }
                    check(d.get_io$$sourcePort()==get16(wire,0) &&
                          d.get_io$$destinationPort()==get16(wire,2),"UDP port byte order");
                    check(d.get_io$$payloadLength()==unsigned(declared>=8?declared-8:0),"UDP payload length");
                    check(bool(d.get_io$$checksumPresent())==hasChecksum,"UDP checksum omission");
                } else {
                    check((d.get_io$$errors()&(mode==11?2:1))!=0,"malformed UDP accepted");
                }
                if(d.get_io$$errors()) ++rejected; else ++passed;
                seen=true;
            }
            if(driving&&d.get_io$$ready()) {
                offset+=count;done=offset==wire.size();driving=false;
            }
            check(++timeout<30000,"UDP pipeline timeout");
        }
    }
    std::cout<<"UDP_VALIDATE_PASS packets="<<passed+rejected<<" accepted="<<passed
             <<" rejected="<<rejected<<" cycles="<<cycles<<" verdict_stalls="<<heldCycles<<'\n';
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
