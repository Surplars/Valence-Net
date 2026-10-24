#include "TcpSackSnapshotHarness.h"
#include "gsim_model_init.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

static void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
struct Range {uint32_t l,r;unsigned age;};
struct Probe {
    STcpSackSnapshotHarness d;
    unsigned cycles=0, cases=0, maxLatency=0;
    Probe(){
        initializeGsimModel(d,&STcpSackSnapshotHarness::init);
        d.set_io$$write(0);d.set_io$$changed(0);d.set_io$$clear(0);
        d.set_io$$valid(0);d.set_io$$base(0);
        d.set_reset(1);step();step();d.set_reset(0);step();
    }
    void step(){d.step();++cycles;}
    void load(const std::array<Range,8>& input,unsigned mask,uint32_t base){
        d.set_io$$write(1);
        for(unsigned i=0;i<8;++i){
            d.set_io$$index(i);d.set_io$$left(input[i].l);d.set_io$$right(input[i].r);
            d.set_io$$age(input[i].age);step();
        }
        d.set_io$$write(0);d.set_io$$valid(mask);d.set_io$$base(base);
    }
    void trigger(){d.set_io$$changed(1);step();d.set_io$$changed(0);}
    void verify(const std::array<Range,8>& input,unsigned mask,uint32_t base){
        std::vector<Range> ordered;
        for(unsigned i=0;i<8;++i)if(mask&(1u<<i))ordered.push_back({input[i].l-base,input[i].r-base,input[i].age});
        std::stable_sort(ordered.begin(),ordered.end(),[](auto a,auto b){return a.l<b.l;});
        std::vector<Range> merged;
        for(auto r:ordered){
            if(!merged.empty()&&merged.back().r==r.l){
                merged.back().r=r.r;merged.back().age=std::min(merged.back().age,r.age);
            }else merged.push_back(r);
        }
        std::stable_sort(merged.begin(),merged.end(),[](auto a,auto b){return a.age<b.age;});
        unsigned count=std::min<size_t>(4,merged.size());
        check(d.get_io$$count()==count,"snapshot count");
        std::array<uint32_t,4> l={d.get_io$$left0(),d.get_io$$left1(),d.get_io$$left2(),d.get_io$$left3()};
        std::array<uint32_t,4> r={d.get_io$$right0(),d.get_io$$right1(),d.get_io$$right2(),d.get_io$$right3()};
        for(unsigned i=0;i<4;++i){
            check(l[i]==(i<count?merged[i].l+base:0),"snapshot left/order");
            check(r[i]==(i<count?merged[i].r+base:0),"snapshot right/merge");
        }
        ++cases;
    }
    void settle(){
        unsigned latency=0;
        while(d.get_io$$busy()){step();check(++latency<180,"snapshot did not converge");}
        maxLatency=std::max(maxLatency,latency);
        step();
    }
};
int main(){
    try{
        Probe p;std::mt19937 rng(0x5ac2026);
        for(unsigned trial=0;trial<2048;++trial){
            uint32_t base=trial%3==0?0xfffffff0u:static_cast<uint32_t>(rng());
            std::array<Range,8> input{};
            uint32_t offset=1;
            for(unsigned i=0;i<8;++i){
                unsigned length=1+rng()%100;
                input[i]={base+offset,base+offset+length,static_cast<unsigned>(rng()%8)};
                offset+=length+((rng()%3)?1+rng()%32:0);
            }
            std::shuffle(input.begin(),input.end(),rng);
            unsigned mask=trial<256?trial:rng()&255;
            p.load(input,mask,base);p.trigger();p.settle();p.verify(input,mask,base);
            // Live changes after atomic capture must not corrupt that snapshot.
            if(trial%8==0){
                p.trigger();
                auto replacement=input;for(auto& r:replacement){r.l+=17;r.r+=17;r.age=(r.age+3)%8;}
                p.load(replacement,mask,base);p.settle();p.verify(input,mask,base);
                // A later changed pulse must supersede all earlier scratch work.
                p.trigger();
                for(unsigned n=0;n<trial%39;++n)p.step();
                p.load(input,mask,base);p.trigger();
                check(p.d.get_io$$count()==0,"dirty snapshot remained visible");
                p.settle();p.verify(input,mask,base);
            }
            if(trial%16==0){
                p.trigger();for(unsigned n=0;n<trial%43;++n)p.step();
                p.d.set_io$$clear(1);p.step();p.d.set_io$$clear(0);p.step();
                check(!p.d.get_io$$busy()&&p.d.get_io$$count()==0,"clear failed");
                p.trigger();p.settle();p.verify(input,mask,base);
            }
        }
        std::cout<<"sack_snapshot PASS cases="<<p.cases<<" cycles="<<p.cycles
                 <<" max_settle="<<p.maxLatency<<"\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
