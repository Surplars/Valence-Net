#include "MemoryQueueHarness.h"
#include "gsim_model_init.h"
#include <cstdint>
#include <deque>
#include <iostream>
#include <random>
#include <stdexcept>
#include <utility>

static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main() { try {
    SMemoryQueueHarness d;
    initializeGsimModel(d, &SMemoryQueueHarness::init);
    std::mt19937_64 random(0x128b20261025ULL);
    std::deque<std::pair<uint64_t,uint64_t>> expected[3];
    const unsigned depth[3] = {2,3,16};
    unsigned cycles=0, overlap=0, collisions=0, stalls=0;
    auto reset = [&] {
        d.set_io$$valid(0); d.set_io$$outReady(0); d.set_io$$select(0);
        d.set_reset(1); d.step(); d.step(); d.set_reset(0);
        for(auto& q:expected) q.clear();
    };
    reset();
    // Alternating fill/drain, full-duplex streaming and long stalled heads.
    // Switching selected queues leaves the other RAM read ports prefetching.
    for (unsigned epoch=0; epoch<5; ++epoch) {
        for (unsigned i=0; i<100000; ++i) {
            unsigned selected=random()%3, phase=(i/64)%5;
            bool valid=phase==0 || phase==2 || (phase==4 && random()%4);
            bool ready=phase==1 || phase==2 || (phase==4 && random()%4);
            auto value=std::pair{random(),random()};
            d.set_io$$select(selected); d.set_io$$valid(valid); d.set_io$$outReady(ready);
            d.set_io$$lo(value.first); d.set_io$$hi(value.second);
            d.step(); ++cycles;
            auto& q=expected[selected];
            check(d.get_io$$count()==q.size(),"FIFO count differs from independent scoreboard");
            check(bool(d.get_io$$ready())==(q.size()<depth[selected]),"FIFO full ready");
            check(bool(d.get_io$$outValid())==!q.empty(),"FIFO empty valid");
            if(!q.empty()) {
                check(d.get_io$$outLo()==q.front().first && d.get_io$$outHi()==q.front().second,
                      "synchronous FIFO head corruption/collision/stall");
                if(!ready) ++stalls;
            }
            bool enq=valid && d.get_io$$ready(), deq=ready && d.get_io$$outValid();
            if(enq && (q.empty() || (q.size()==1 && deq))) ++collisions;
            if(enq && deq) ++overlap;
            if(deq) q.pop_front();
            if(enq) q.push_back(value);
        }
        reset(); // Reset with unread data must make stale RAM inaccessible.
    }
    check(collisions>1000 && overlap>1000 && stalls>1000,"insufficient FIFO corner coverage");
    std::cout<<"MEMORY_QUEUE_PASS cycles="<<cycles<<" forwarding_cases="<<collisions
             <<" simultaneous="<<overlap<<" stalled_heads="<<stalls<<" depths=2,3,16\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }}
