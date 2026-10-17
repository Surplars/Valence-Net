#include "ChecksumHarness.h"
#include <cstdint>
#include <deque>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }

// Independent byte-serial implementation; no RTL folding/tree code is shared.
static uint16_t reference(const std::vector<uint8_t>& bytes, uint16_t seed) {
    uint32_t sum = seed;
    for (size_t i=0; i<bytes.size(); ++i) {
        sum += uint32_t(bytes[i]) << ((i&1) ? 0 : 8);
        if (sum > 65535) sum -= 65535;
    }
    return uint16_t(~sum);
}

struct Beat { uint64_t lo=0, hi=0; uint16_t keep=0, seed=0; bool last=false, error=false; };
struct Result { uint16_t checksum; bool error; };

int main() { try {
    SChecksumHarness d;
    std::mt19937_64 rng(0x10717689293ULL);
    unsigned packets=0, beats=0, cycles=0, stalls=0;
    auto reset=[&] {
        d.set_io$$valid(0); d.set_io$$outReady(0);
        d.set_reset(1); d.step(); d.step(); d.set_reset(0);
    };
    reset();
    check(reference({0x00,0x01,0xf2,0x03,0xf4,0xf5,0xf6,0xf7},0)==0x220d,
          "RFC 1071 known vector");
    for (unsigned epoch=0; epoch<4; ++epoch) {
        std::deque<Beat> inputs;
        std::deque<Result> expected;
        for (unsigned p=0;p<1600;++p) {
            const size_t length = p<160 ? p : rng()%9217;
            const uint16_t seed = p%3 ? uint16_t(rng()) : 0;
            std::vector<uint8_t> data(length);
            for (auto& b:data) b=uint8_t(rng());
            if(p%17==0) for(auto& b:data) b=0xff;
            bool bad = p%11==0;
            size_t offset=0;
            do {
                // Arbitrary partial beats exercise cross-beat odd parity.
                size_t count=std::min(length-offset, size_t(1+rng()%16));
                Beat b;
                b.seed=offset==0 ? seed : uint16_t(rng());
                b.keep=uint16_t((1U<<count)-1);
                b.lo=rng(); b.hi=rng(); // Invalid lanes must be ignored.
                for(size_t i=0;i<count;++i) {
                    uint64_t& word=i<8?b.lo:b.hi;
                    unsigned shift=8*(i%8);
                    word=(word&~(uint64_t(255)<<shift))|(uint64_t(data[offset+i])<<shift);
                }
                offset+=count;
                b.last=offset==length;
                b.error=bad && (offset==count);
                inputs.push_back(b);
            } while(offset<length);
            expected.push_back({reference(data,seed),bad});
        }
        bool driving=false, held=false;
        Result heldResult{};
        unsigned idle=0;
        while(!inputs.empty() || !expected.empty()) {
            bool outReady=(rng()%7)<4;
            if(!driving && !inputs.empty() && rng()%5) driving=true;
            d.set_io$$outReady(outReady);
            d.set_io$$valid(driving);
            if(driving) {
                auto b=inputs.front();
                d.set_io$$lo(b.lo); d.set_io$$hi(b.hi); d.set_io$$keep(b.keep);
                d.set_io$$seed(b.seed); d.set_io$$last(b.last); d.set_io$$error(b.error);
            }
            d.step(); ++cycles;
            if(held) check(d.get_io$$outValid() && d.get_io$$checksum()==heldResult.checksum &&
                           bool(d.get_io$$bad())==heldResult.error,"result changed under backpressure");
            held=d.get_io$$outValid() && !outReady;
            if(held) { heldResult={uint16_t(d.get_io$$checksum()),bool(d.get_io$$bad())}; ++stalls; }
            if(d.get_io$$outValid() && outReady) {
                check(!expected.empty(),"unexpected duplicate result");
                auto e=expected.front(); expected.pop_front(); ++packets;
                if(d.get_io$$checksum()!=e.checksum || bool(d.get_io$$bad())!=e.error) {
                    std::cerr<<"packet="<<packets<<" got="<<d.get_io$$checksum()<<" expected="<<e.checksum<<'\n';
                    throw std::runtime_error("checksum/error oracle mismatch");
                }
                idle=0;
            }
            if(driving && d.get_io$$ready()) { inputs.pop_front(); driving=false; ++beats; idle=0; }
            check(++idle<1000,"checksum pipeline deadlock");
        }
        reset();
    }
    // Every illegal sparse keep mask must poison the result, then recover.
    for(unsigned mask=0;mask<65536;++mask) {
        bool legal=(mask&(mask+1))==0;
        d.set_io$$valid(1); d.set_io$$outReady(1); d.set_io$$keep(mask);
        d.set_io$$last(1); d.set_io$$error(0); d.set_io$$seed(0);
        d.step();
        d.set_io$$valid(0); d.step();
        check(d.get_io$$outValid() && bool(d.get_io$$bad())==!legal,"keep-mask validation");
    }
    // A reset during an unfinished frame discards its sum/error/parity.
    d.set_io$$valid(1); d.set_io$$keep(1); d.set_io$$last(0); d.set_io$$error(1); d.step();
    reset();
    d.set_io$$valid(1); d.set_io$$outReady(1); d.set_io$$keep(0);
    d.set_io$$last(1); d.set_io$$error(0); d.set_io$$seed(0); d.step();
    d.set_io$$valid(0); d.step();
    check(d.get_io$$outValid() && !d.get_io$$bad() && d.get_io$$checksum()==65535,"reset recovery");
    std::cout<<"CHECKSUM_PASS packets="<<packets<<" beats="<<beats<<" cycles="<<cycles
             <<" stalled_results="<<stalls<<" keep_masks=65536\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
