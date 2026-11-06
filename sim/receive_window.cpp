#include "ReceiveWindowHarness.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

static void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
static uint8_t pattern(uint32_t position) { return (position * 37u + position / 251u) & 255u; }

struct Peer {
    std::unique_ptr<SReceiveWindowHarness> model = std::make_unique<SReceiveWindowHarness>();
    SReceiveWindowHarness& d = *model;
    bool reading = false;
    uint64_t cycles = 0, updates = 0;
    std::vector<uint8_t> received;
    std::vector<uint32_t> updateWindows;
    Peer() {
        d.set_io$$clear(0); d.set_io$$offerValid(0); d.set_io$$sequence(0); d.set_io$$length(0);
        d.set_io$$inValid(0); d.set_io$$inLo(0); d.set_io$$inHi(0); d.set_io$$inKeep(0); d.set_io$$inLast(0);
        d.set_io$$outReady(0); d.set_reset(1); tick(); tick(); d.set_reset(0); idle(16);
    }
    void tick() {
        d.set_io$$outReady(reading); d.step(); ++cycles;
        require(!d.get_io$$fault(), "receive queue fault");
        require(d.get_io$$free() <= 262144, "credit overflow");
        if (d.get_io$$update()) { ++updates; updateWindows.push_back(d.get_io$$free()); }
        if (reading && d.get_io$$outValid()) {
            uint64_t lo = d.get_io$$outLo(), hi = d.get_io$$outHi();
            unsigned keep = d.get_io$$outKeep();
            require(keep && !(keep & (keep + 1)), "invalid output keep");
            for (unsigned i = 0; i < 16; ++i) if (keep & (1u << i))
                received.push_back((i < 8 ? lo >> (8*i) : hi >> (8*(i-8))) & 255);
        }
    }
    void idle(unsigned count) { while (count--) tick(); }
    void until(const std::function<bool()>& condition, unsigned limit = 100000) {
        while (!condition() && limit--) tick();
        require(condition(), "timeout; free=" + std::to_string(d.get_io$$free()));
    }
    void send(uint32_t sequence, unsigned length) {
        require(length && length <= 1460, "test segment size");
        d.set_io$$sequence(sequence); d.set_io$$length(length); d.set_io$$offerValid(1);
        unsigned limit = 100000;
        do { require(limit--, "metadata timeout"); tick(); } while (!d.get_io$$offerReady());
        d.set_io$$offerValid(0);
        for (unsigned offset = 0; offset < length; offset += 16) {
            unsigned count = std::min(16u, length - offset);
            uint64_t lo = 0, hi = 0;
            for (unsigned i = 0; i < count; ++i) {
                uint64_t byte = pattern(sequence + offset + i);
                if (i < 8) lo |= byte << (8*i); else hi |= byte << (8*(i-8));
            }
            d.set_io$$inLo(lo); d.set_io$$inHi(hi); d.set_io$$inKeep((1u << count) - 1);
            d.set_io$$inLast(offset + count == length); d.set_io$$inValid(1);
            limit = 100000;
            do { require(limit--, "payload timeout"); tick(); } while (!d.get_io$$inReady());
        }
        d.set_io$$inValid(0); d.set_io$$inLast(0);
    }
    void verify(uint32_t base = 0) {
        for (size_t i = 0; i < received.size(); ++i)
            require(received[i] == pattern(base + i), "payload mismatch at " + std::to_string(i));
    }
    void clear() {
        reading = false; d.set_io$$clear(1); tick(); tick(); d.set_io$$clear(0); idle(32);
        received.clear(); updates = 0; updateWindows.clear();
        require(d.get_io$$free() == 262144 && d.get_io$$empty(), "clear did not release all ownership");
    }
};

int main() { try {
    Peer p; p.reading = true;
    for (unsigned i = 0; i < 256; ++i) p.send(i*1460, 1460);
    p.until([&] { return p.received.size() == 256*1460; }); p.idle(100); p.verify();
    require(p.updates == 0, "redundant high-window notification");
    require(p.d.get_io$$free() == 262144, "fast sink leaked credit");
    std::cout << "PASS_FAST_SINK bytes=" << p.received.size() << " window_updates=0\n";

    p.clear(); uint32_t sequence = 0;
    for (unsigned round = 0; round < 3; ++round) {
        p.reading = false;
        for (unsigned left = 262144; left;) {
            unsigned length = std::min(left, 1460u); p.send(sequence, length); sequence += length; left -= length;
        }
        p.until([&] { return p.d.get_io$$free() == 0; }); p.idle(40);
        size_t previous = p.received.size(); uint64_t updates = p.updates;
        p.reading = true; p.until([&] { return p.received.size() >= previous + 16; });
        p.reading = false; p.idle(100);
        require(p.received.size() == previous + 16 && p.d.get_io$$free() == 16, "partial reopen credit");
        require(p.updates > updates, "zero-window reopening notification missing");
        p.reading = true; p.until([&] { return p.received.size() == (round+1)*262144; }); p.idle(100);
        require(p.d.get_io$$free() == 262144 && p.d.get_io$$empty(), "full drain lost credit");
        require(std::find(p.updateWindows.begin(), p.updateWindows.end(), 262144) != p.updateWindows.end(),
                "full-window restoration notification missing");
        p.verify();
    }
    std::cout << "PASS_ZERO_WINDOW rounds=3 bytes=" << p.received.size() << " partial_reopen=16 wrap=PASS\n";

    p.clear(); p.reading = true;
    p.send(1460,1460); p.idle(300);
    require(p.received.empty() && p.d.get_io$$sackLength() == 12 && p.d.get_io$$sackLeft() == 1460 &&
            p.d.get_io$$sackRight() == 2920 && p.updates, "SACK notification suppressed");
    p.send(0,1460); p.until([&] { return p.received.size() == 2920; }); p.idle(300); p.verify();
    require(p.d.get_io$$sackLength() == 0 && p.d.get_io$$free() == 262144 && p.d.get_io$$empty(),
            "gap replay or SACK retirement failed");
    std::cout << "PASS_SACK gap_bytes=1460 replay_bytes=1460 encoded_after_fill=0\n";
    std::cout << "RECEIVE_WINDOW_PASS capacity=262144 high_window_updates=0 zero_reopen=PASS full_restore=PASS wrap=PASS SACK=PASS\n";
    return 0;
} catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; } }
