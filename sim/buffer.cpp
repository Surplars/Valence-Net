#include "BufferHarness.h"
#include <cstdint>
#include <deque>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

static void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
struct Beat {uint64_t lo,hi;uint16_t keep;bool last,error;};
static bool same(const Beat& a,const Beat& b) {
    return a.lo==b.lo && a.hi==b.hi && a.keep==b.keep && a.last==b.last;
}
int main(){try {
    SBufferHarness d;
    std::mt19937_64 rng(0xbaff20261017ULL);
    unsigned packets=0,drops=0,outputs=0,cycles=0,overlap=0,stalls=0;
    auto reset=[&] {
        d.set_io$$valid(0);d.set_io$$verdictValid(0);d.set_io$$outReady(0);
        d.set_reset(1);d.step();d.step();d.set_reset(0);
    };
    reset();
    for(unsigned epoch=0;epoch<4;++epoch) {
        std::deque<Beat> expected;
        std::vector<Beat> packet;
        unsigned done=0,index=0,idle=0;
        bool accept=false,poison=false,drive=false,driveVerdict=false,held=false;
        Beat heldBeat{};
        while(done<1000 || !expected.empty()) {
            if(packet.empty() && done<1000) {
                unsigned size=1+rng()%25;
                accept=rng()%5!=0;poison=rng()%7==0;
                for(unsigned i=0;i<size;++i) packet.push_back({rng(),rng(),
                    uint16_t(i+1==size?((1U<<(1+rng()%16))-1):65535),i+1==size,poison&&i==size/2});
                index=0;
            }
            if(!drive && index<packet.size() && rng()%5) drive=true;
            if(!driveVerdict && !packet.empty() && index==packet.size() && rng()%4==0) driveVerdict=true;
            const bool outReady=rng()%7<3;
            d.set_io$$outReady(outReady);d.set_io$$valid(drive);
            d.set_io$$verdictValid(driveVerdict);d.set_io$$accept(accept);
            if(drive) {
                auto b=packet[index];d.set_io$$lo(b.lo);d.set_io$$hi(b.hi);d.set_io$$keep(b.keep);
                d.set_io$$last(b.last);d.set_io$$error(b.error);
            }
            d.step();++cycles;
            Beat out{d.get_io$$outLo(),d.get_io$$outHi(),uint16_t(d.get_io$$outKeep()),bool(d.get_io$$outLast()),false};
            if(held) check(d.get_io$$outValid()&&same(heldBeat,out),"packet buffer output changed while stalled");
            held=d.get_io$$outValid()&&!outReady;
            if(held) {heldBeat=out;++stalls;}
            bool progress=false;
            if(d.get_io$$outValid()&&outReady) {
                check(!expected.empty(),"uncommitted, dropped or duplicate bytes escaped packet buffer");
                check(same(expected.front(),out),"packet buffer corruption/order/last mismatch");
                expected.pop_front();++outputs;progress=true;
                if(drive&&d.get_io$$ready()) ++overlap;
            }
            if(drive&&d.get_io$$ready()) {++index;drive=false;progress=true;}
            if(driveVerdict&&d.get_io$$verdictReady()) {
                const bool drop=!accept||poison||packet.size()>16;
                check(bool(d.get_io$$dropped())==drop,"packet drop decision mismatch");
                if(drop) ++drops; else for(auto b:packet) expected.push_back(b);
                ++packets;++done;packet.clear();index=0;driveVerdict=false;progress=true;
            } else check(!d.get_io$$dropped(),"spurious packet drop pulse");
            idle=progress?0:idle+1;
            check(idle<1000,"packet buffer deadlock");
        }
        reset();
    }
    // Reset abandons both an unfinished receive and committed unread contents.
    for(unsigned complete=0;complete<2;++complete) {
        d.set_io$$valid(1);d.set_io$$last(complete);d.set_io$$error(0);d.set_io$$keep(65535);
        d.step();check(d.get_io$$ready(),"reset scenario write readiness");
        d.set_io$$valid(0);
        if(complete) {d.set_io$$verdictValid(1);d.set_io$$accept(1);d.step();}
        reset();d.set_io$$outReady(1);
        for(unsigned i=0;i<12;++i) {d.step();check(!d.get_io$$outValid(),"pre-reset bytes escaped buffer");}
    }
    check(overlap>100,"double-bank receive/transmit overlap not exercised");
    std::cout<<"PACKET_BUFFER_PASS packets="<<packets<<" dropped="<<drops<<" output_beats="<<outputs
             <<" concurrent_rx_tx="<<overlap<<" stall_cycles="<<stalls<<" cycles="<<cycles<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
