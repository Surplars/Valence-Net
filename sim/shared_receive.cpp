#include "SharedTcpReceiveHarness.h"
#include "gsim_model_init.h"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
#include <tuple>
#include <vector>
static void check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
static void put(std::vector<uint8_t>& v,unsigned p,unsigned n,uint64_t x){
    for(unsigned i=0;i<n;++i)v.at(p+i)=x>>(8*(n-i-1));
}
static uint16_t sum(std::vector<uint8_t> v){
    if(v.size()%2)v.push_back(0);unsigned s=0;
    for(unsigned i=0;i<v.size();i+=2)s+=(unsigned(v[i])<<8)|v[i+1];
    while(s>>16)s=(s&65535)+(s>>16);return uint16_t(~s);
}
struct Packet{uint64_t tag;unsigned id,kind;std::vector<uint8_t> wire,payload;};
int main(){try{
    SSharedTcpReceiveHarness d;initializeGsimModel(d,&SSharedTcpReceiveHarness::init);
    std::mt19937_64 rng(0x6453920261025ULL);
    std::deque<Packet> input,expected;
    std::map<uint64_t,unsigned> drops;
    std::vector<bool> complete(1200,false);
    for(unsigned id=0;id<complete.size();++id){
        Packet p{rng(),id,id%8,{},{}};
        unsigned n=id%6==0?0:(id%6==1?1:unsigned(rng()%402));
        p.payload.resize(n);for(auto& b:p.payload)b=rng();
        std::vector<uint8_t> tcp(20);
        put(tcp,0,2,8000+id%4);put(tcp,2,2,12345);put(tcp,4,4,id);
        tcp[12]=0x50;tcp[13]=0x18;put(tcp,14,2,65535);
        tcp.insert(tcp.end(),p.payload.begin(),p.payload.end());
        std::vector<uint8_t> pseudo(12);put(pseudo,0,4,0x0a000002+id%4);
        put(pseudo,4,4,0x0a000001);pseudo[9]=6;put(pseudo,10,2,tcp.size());
        pseudo.insert(pseudo.end(),tcp.begin(),tcp.end());put(tcp,16,2,sum(pseudo));
        if(p.kind==1)tcp[16]^=1; // Valid IP, bad TCP checksum.
        p.wire.resize(20);p.wire[0]=0x45;p.wire[8]=64;p.wire[9]=p.kind==4?17:6;
        put(p.wire,2,2,20+tcp.size());put(p.wire,12,4,0x0a000002+id%4);put(p.wire,16,4,0x0a000001);
        if(p.kind==5)put(p.wire,6,2,0x2000); // Unsupported IP fragment.
        put(p.wire,10,2,sum(p.wire));
        if(p.kind==2)p.wire[10]^=1; // IP checksum rejection.
        p.wire.insert(p.wire.end(),tcp.begin(),tcp.end());
        if(p.kind==1)drops.emplace(p.tag,1);
        else if(p.kind==2 || p.kind==3 || p.kind==5)drops.emplace(p.tag,0);
        else if(p.kind==4)drops.emplace(p.tag,2);
        else expected.push_back(p);
        input.push_back(p);
    }
    d.set_io$$valid(0);d.set_io$$metaReady(0);d.set_io$$outReady(0);
    d.set_reset(1);d.step();d.step();d.set_reset(0);
    unsigned offset=0,pos=0,cycles=0,good=0,rejected=0,stall=0;
    bool driving=false;std::optional<Packet> active;
    using Beat=std::tuple<uint64_t,uint64_t,unsigned,bool>;
    std::optional<Beat> held;
    while(!input.empty()||!expected.empty()||active||!drops.empty()){
        bool mr=(cycles%193<140 && rng()%4),pr=(cycles%137<90 && rng()%4);
        if(!input.empty() && !driving && rng()%5)driving=true;
        d.set_io$$valid(driving);d.set_io$$metaReady(mr);d.set_io$$outReady(pr);
        unsigned n=0;
        if(driving){
            auto&p=input.front();n=std::min(16U,unsigned(p.wire.size())-offset);
            uint64_t lo=0,hi=0;
            for(unsigned i=0;i<n;++i)(i<8?lo:hi)|=uint64_t(p.wire[offset+i])<<(8*(i%8));
            d.set_io$$lo(lo);d.set_io$$hi(hi);d.set_io$$keep((1U<<n)-1);
            d.set_io$$last(offset+n==p.wire.size());d.set_io$$error(p.kind==3 && offset==0);
            // Only the first accepted beat owns the tag; later values are poison.
            d.set_io$$tag(offset?~p.tag:p.tag);
        }
        d.step();++cycles;
        if(d.get_io$$metaValid()){
            check(!active && !expected.empty(),"shared metadata ownership/order");
            auto&p=expected.front();
            check(complete[p.id],"metadata escaped before full validation");
            check(d.get_io$$metaTag()==p.tag && d.get_io$$sequence()==p.id &&
                  d.get_io$$length()==p.payload.size(),"shared metadata tag misassociated");
            if(mr){active=p;expected.pop_front();pos=0;}
        }
        Beat beat{d.get_io$$outLo(),d.get_io$$outHi(),unsigned(d.get_io$$outKeep()),bool(d.get_io$$outLast())};
        if(held)check(d.get_io$$outValid() && beat==*held,"shared payload changed while stalled");
        held=d.get_io$$outValid()&&!pr?std::optional<Beat>(beat):std::nullopt;
        if(held)++stall;
        if(d.get_io$$outValid()&&pr){
            check(active.has_value() && !d.get_io$$outError(),"shared payload escaped without metadata");
            unsigned count=__builtin_popcount(d.get_io$$outKeep());
            check(pos+count<=active->payload.size(),"shared payload overflow");
            for(unsigned i=0;i<count;++i)check(uint8_t((i<8?d.get_io$$outLo():d.get_io$$outHi())>>(8*(i%8)))==active->payload[pos+i],"shared payload corruption");
            pos+=count;
            if(d.get_io$$outLast()){check(pos==active->payload.size(),"shared payload truncated");active.reset();++good;}
        }
        unsigned mask=d.get_io$$dropMask();
        const uint64_t tags[3]={d.get_io$$drop0(),d.get_io$$drop1(),d.get_io$$drop2()};
        for(unsigned i=0;i<3;++i)if(mask&(1U<<i)){
            auto found=drops.find(tags[i]);check(found!=drops.end()&&found->second==i,"shared drop tag wrong/duplicate");
            drops.erase(found);++rejected;
        }
        if(driving && d.get_io$$ready()){
            offset+=n;driving=false;
            if(offset==input.front().wire.size()){complete[input.front().id]=true;input.pop_front();offset=0;}
        }
        check(cycles<500000,"shared frontend deadlock");
    }
    check(good==450 && rejected==750 && stall>100,"shared receive coverage");
    d.set_io$$valid(0);d.set_io$$outReady(1);d.set_io$$metaReady(1);
    for(unsigned i=0;i<100;++i){d.step();check(!d.get_io$$metaValid()&&!d.get_io$$outValid()&&!d.get_io$$dropMask(),"shared duplicate retirement");}
    std::cout<<"SHARED_RECEIVE_PASS packets=1200 good="<<good<<" rejected="<<rejected
             <<" stalled_payload_cycles="<<stall<<" cycles="<<cycles<<" immutable_tags=pass\n";
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
