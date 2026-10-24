#include "TcpClosedHarness.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void put(std::vector<uint8_t>&v,unsigned p,unsigned n,uint64_t x){
    for(unsigned i=0;i<n;++i)v[p+i]=uint8_t(x>>(8*(n-i-1)));
}
static uint32_t get(const std::vector<uint8_t>&v,unsigned p,unsigned n){
    uint32_t x=0;for(unsigned i=0;i<n;++i)x=(x<<8)|v.at(p+i);return x;
}
static uint16_t sum(std::vector<uint8_t>v){
    if(v.size()%2)v.push_back(0);uint32_t x=0;
    for(unsigned i=0;i<v.size();i+=2)x+=(v[i]<<8)|v[i+1];
    while(x>>16)x=(x&65535)+(x>>16);return uint16_t(~x);
}
static uint16_t tcpSum(uint32_t src,uint32_t dst,const std::vector<uint8_t>&v){
    std::vector<uint8_t>p(12);put(p,0,4,src);put(p,4,4,dst);p[9]=6;put(p,10,2,v.size());
    p.insert(p.end(),v.begin(),v.end());return sum(p);
}
static std::vector<uint8_t> packet(unsigned flags=2,uint32_t seq=100,uint32_t ack=200,
    unsigned length=0,uint32_t src=0x0a000002,uint32_t dst=0x0a000001,
    std::vector<uint8_t>options={}){
    while(options.size()%4)options.push_back(0);
    std::vector<uint8_t>v(40+options.size()+length);
    v[0]=0x45;put(v,2,2,v.size());put(v,4,2,7);put(v,6,2,0x4000);v[8]=64;v[9]=6;
    put(v,12,4,src);put(v,16,4,dst);
    put(v,20,2,8080);put(v,22,2,9000);put(v,24,4,seq);put(v,28,4,ack);
    v[32]=uint8_t((5+options.size()/4)<<4);v[33]=flags;put(v,34,2,4000);
    std::copy(options.begin(),options.end(),v.begin()+40);
    for(unsigned i=0;i<length;++i)v[40+options.size()+i]=uint8_t(i*37+11);
    std::vector<uint8_t>tcp(v.begin()+20,v.end());put(v,36,2,tcpSum(src,dst,tcp));
    put(v,10,2,sum(std::vector<uint8_t>(v.begin(),v.begin()+20)));return v;
}
static void ipChecksum(std::vector<uint8_t>&v){put(v,10,2,0);put(v,10,2,sum(std::vector<uint8_t>(v.begin(),v.begin()+20)));}
static void tcpChecksum(std::vector<uint8_t>&v){put(v,36,2,0);put(v,36,2,tcpSum(get(v,12,4),get(v,16,4),std::vector<uint8_t>(v.begin()+20,v.end())));}

struct Bench {
    STcpClosedHarness d;
    std::vector<std::vector<uint8_t>> frames;
    std::vector<uint8_t> current;
    std::mt19937_64 rng{0x92930004};
    bool blocked=false, held=false, ticking=true;
    uint64_t heldLo=0,heldHi=0;unsigned heldKeep=0,heldDest=0;bool heldLast=false;
    unsigned cycles=0,generated=0,drops=0;
    Bench(){
        d.set_io$$localIp(0x0a000001);d.set_io$$netmask(0xffffff00);d.set_io$$tick(0);
        d.set_io$$valid(0);d.set_io$$error(0);d.set_io$$last(0);d.set_io$$keep(0);
        d.set_io$$outReady(0);d.set_reset(1);d.step();d.step();d.set_reset(0);
    }
    void step(){
        bool ready=!blocked;d.set_io$$outReady(ready);d.set_io$$tick(ticking);
        d.step();++cycles;
        check(d.get_io$$ready(),"exception input backpressured shared ingress");
        if(held)check(d.get_io$$outValid()&&heldLo==d.get_io$$outLo()&&
            heldHi==d.get_io$$outHi()&&heldKeep==d.get_io$$outKeep()&&
            heldLast==bool(d.get_io$$outLast())&&heldDest==d.get_io$$destination(),
            "reset output/metadata changed under backpressure");
        held=d.get_io$$outValid()&&!ready;
        if(held){heldLo=d.get_io$$outLo();heldHi=d.get_io$$outHi();heldKeep=d.get_io$$outKeep();
            heldLast=d.get_io$$outLast();heldDest=d.get_io$$destination();}
        if(d.get_io$$outValid()&&ready){
            check(!d.get_io$$outError(),"reset output error");unsigned keep=d.get_io$$outKeep();
            check(keep&&(keep&(keep+1))==0,"reset output keep");
            for(unsigned i=0;i<unsigned(__builtin_popcount(keep));++i){
                uint64_t word=i<8?d.get_io$$outLo():d.get_io$$outHi();current.push_back(word>>(8*(i%8)));
            }
            if(d.get_io$$outLast()){
                check(current.size()==40&&get(current,2,2)==40&&current[0]==0x45&&current[9]==6,
                    "reset framing/length");
                check(sum(std::vector<uint8_t>(current.begin(),current.begin()+20))==0,"reset IPv4 checksum");
                check(tcpSum(get(current,12,4),get(current,16,4),std::vector<uint8_t>(current.begin()+20,current.end()))==0,"reset TCP checksum");
                check(get(current,16,4)==d.get_io$$destination(),"reset destination metadata");
                check(current[32]==0x50&&get(current,34,2)==0&&get(current,38,2)==0,
                    "reset unexpected options/window/urgent");
                frames.push_back(current);current.clear();
            }else check(keep==65535,"reset short nonfinal beat");
        }
        generated+=d.get_io$$generated();drops+=d.get_io$$dropped();
    }
    void idle(unsigned n=140){d.set_io$$valid(0);for(unsigned i=0;i<n;++i)step();}
    void wire(const std::vector<uint8_t>&v,bool error=false,bool badKeep=false,bool gaps=false){
        for(unsigned offset=0;offset<v.size();offset+=16){
            if(gaps&&rng()%3==0)idle(rng()%4+1);
            unsigned n=std::min(16U,unsigned(v.size())-offset);uint64_t lo=0,hi=0;
            for(unsigned i=0;i<n;++i){auto&w=i<8?lo:hi;w|=uint64_t(v[offset+i])<<(8*(i%8));}
            d.set_io$$valid(1);d.set_io$$lo(lo);d.set_io$$hi(hi);
            d.set_io$$keep(((1U<<n)-1)^(badKeep&&offset==0?2:0));
            d.set_io$$last(offset+n==v.size());d.set_io$$error(error&&offset+n==v.size());step();
        }d.set_io$$valid(0);
    }
    void expect(const std::vector<uint8_t>&v,bool respond=true,bool error=false,bool badKeep=false){
        auto before=frames.size();wire(v,error,badKeep,true);idle();
        check(frames.size()==before+(respond?1:0),"unexpected reset count for input");
        if(respond){
            const auto&r=frames.back();unsigned flags=v[33];unsigned tcpHeader=(v[32]>>4)*4;
            uint32_t end=get(v,24,4)+get(v,2,2)-20-tcpHeader+bool(flags&2)+bool(flags&1);
            check(get(r,12,4)==get(v,16,4)&&get(r,16,4)==get(v,12,4)&&
                get(r,20,2)==get(v,22,2)&&get(r,22,2)==get(v,20,2),"reset tuple reversal");
            check(r[33]==((flags&16)?4:20)&&get(r,24,4)==((flags&16)?get(v,28,4):0)&&
                get(r,28,4)==((flags&16)?0:end),"reset sequence/acknowledgement");
        }
    }
};

int main(){try{
    Bench b;
    for(unsigned f=0;f<256;++f)
        b.expect(packet(f,0xfffffffeU,0x12345678U,f%37),!(f&4));
    b.expect(packet(3,0xfffffff0U,0,127));
    b.expect(packet(2,17,0,7,0x0a000002,0x0a000001,{2,4,5,180,4,2,1,1}));
    b.expect(packet(2,17,0,7,0x0a000002,0x0a000001,{30,0,0,0}),false);
    for(uint32_t src:{0U,0x01000001U,0x7f000001U,0xe0000001U,0xffffffffU,0x0a0000ffU}){
        b.expect(packet(2,17,0,0,src),src==0x01000001U);
    }
    // A remote subnet may use a different mask: .255 alone is not evidence of broadcast.
    b.expect(packet(2,17,0,0,0x0a0203ffU));
    b.d.set_io$$netmask(0xfffffffeU);b.expect(packet(2,17,0,0,0x0a000000U));
    b.d.set_io$$netmask(0xffffffffU);b.expect(packet(2,17,0,0,0x0a000002U));
    b.d.set_io$$netmask(0xffffff00U);
    b.expect(packet(2,17,0,0,0x0a000002,0xffffffffU),false);
    b.expect(packet(2,17,0,0,0x0a000002,0x0a000003),false);
    auto bad=packet();bad[10]^=1;b.expect(bad,false);
    bad=packet(18,42,123,29);bad.back()^=1;b.expect(bad,false);
    bad=packet();put(bad,6,2,0x2000);ipChecksum(bad);b.expect(bad,false);
    bad=packet();put(bad,6,2,1);ipChecksum(bad);b.expect(bad,false);
    bad=packet();bad[32]=0x40;tcpChecksum(bad);b.expect(bad,false);
    bad=packet();bad[32]=0xf0;tcpChecksum(bad);b.expect(bad,false);
    bad=packet();bad.resize(33);b.expect(bad,false);
    bad=packet();put(bad,2,2,60);ipChecksum(bad);b.expect(bad,false);
    b.expect(packet(),false,true);
    b.expect(packet(),false,false,true);
    b.expect(packet(2,17,0,600),false); // full oversized frame drains without a partial reply
    b.expect(packet(2,18)); // poisoned overflow cannot contaminate the next packet

    // No clock tick => the first reply consumes the bounded rate token.
    Bench rate;rate.ticking=false;rate.expect(packet(2,1));
    rate.expect(packet(2,2),false);
    rate.ticking=true;rate.idle(1);rate.ticking=false;rate.expect(packet(2,3),false);
    rate.ticking=true;rate.idle(1);rate.ticking=false;rate.expect(packet(2,4));
    check(rate.frames.size()==2,"tick rate limit");

    // A blocked serializer remains byte-stable while input is always drained.
    Bench stall;stall.blocked=true;
    for(unsigned i=0;i<24;++i){stall.wire(packet(2,1000+i));stall.idle(100);}
    check(stall.generated<=8&&stall.frames.empty()&&stall.drops>0,"unbounded exception response retention");
    stall.blocked=false;stall.idle(1000);
    check(stall.frames.size()==stall.generated,"accepted response lost after backpressure");

    // Consecutive unknown packets exercise all-or-nothing busy admission.
    Bench flood;
    for(unsigned i=0;i<128;++i)flood.wire(packet(2,10000+i));
    flood.idle(500);
    check(flood.drops>0&&flood.generated>0&&flood.frames.size()==flood.generated,"busy admission accounting");
    for(const auto&r:flood.frames)check(get(r,28,4)>=10001&&get(r,28,4)<=10128,"busy admission mixed packet metadata");
    flood.expect(packet(2,20000)); // fully drained parser is reusable
    std::cout<<"TCP_CLOSED_PASS flags=256 checksum_options_lengths=pass sequence_wrap=pass "
        <<"broadcast_rst_suppression=pass tick_limit=pass stalled_output=pass packet_admission=pass\n";
    return 0;
}catch(const std::exception&e){std::cerr<<"tcp_closed: "<<e.what()<<'\n';return 1;}}
