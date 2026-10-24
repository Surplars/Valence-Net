#include "IcmpErrorsHarness.h"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using Bytes = std::vector<uint8_t>;
static void check(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
static void put16(Bytes& p, unsigned i, uint16_t n) { p.at(i) = n >> 8; p.at(i + 1) = n; }
static void put32(Bytes& p, unsigned i, uint32_t n) { put16(p, i, n >> 16); put16(p, i + 2, n); }
static uint16_t get16(const Bytes& p, unsigned i) { return (unsigned(p.at(i)) << 8) | p.at(i + 1); }
static uint32_t get32(const Bytes& p, unsigned i) { return (uint32_t(get16(p, i)) << 16) | get16(p, i + 2); }
static uint16_t checksum(const Bytes& p) {
    uint32_t s = 0;
    for (unsigned i = 0; i < p.size(); i += 2)
        s += (uint16_t(p[i]) << 8) | (i + 1 < p.size() ? p[i + 1] : 0);
    while (s >> 16) s = (s & 65535) + (s >> 16);
    return ~s;
}
static constexpr uint32_t local = 0xc0000202, peer = 0xc6336407, router = 0xc0000201;
static void fixIp(Bytes& p) {
    put16(p, 10, 0);
    put16(p, 10, checksum(Bytes(p.begin(), p.begin() + 20)));
}
static Bytes ipPacket(uint32_t source, uint32_t destination, uint8_t protocol, const Bytes& payload) {
    Bytes p(20); p[0] = 0x45; put16(p, 2, 20 + payload.size()); put16(p, 4, 0x593a);
    put16(p, 6, 0x4000); p[8] = 64; p[9] = protocol;
    put32(p, 12, source); put32(p, 16, destination); fixIp(p);
    p.insert(p.end(), payload.begin(), payload.end()); return p;
}
static Bytes quoted(uint8_t protocol = 6, uint16_t total = 1500, uint32_t sequence = 0xfffffff0) {
    Bytes transport(protocol == 6 ? 20 : 8);
    put16(transport, 0, 12345); put16(transport, 2, 8080);
    if (protocol == 6) { put32(transport, 4, sequence); transport[12] = 0x50; transport[13] = 0x18; }
    else { put16(transport, 4, total - 20); put16(transport, 6, 0x4a72); }
    Bytes p = ipPacket(local, peer, protocol, transport);
    put16(p, 2, total); fixIp(p); p.resize(28); return p;
}
static Bytes errorPacket(Bytes quote, uint8_t kind = 3, uint8_t code = 4, uint16_t mtu = 576,
                         unsigned suffix = 0, uint32_t source = router) {
    Bytes e(8); e[0] = kind; e[1] = code;
    if (kind == 3 && code == 4) put16(e, 6, mtu);
    if (kind == 12) e[4] = 9;
    e.insert(e.end(), quote.begin(), quote.end());
    for (unsigned i = 0; i < suffix; ++i) e.push_back(uint8_t(i * 31 + 19));
    put16(e, 2, checksum(e)); return ipPacket(source, local, 1, e);
}
static Bytes udpPacket(unsigned payloadLength, bool zeroChecksum = false) {
    Bytes u(8 + payloadLength);
    put16(u, 0, 12345); put16(u, 2, 7777); put16(u, 4, u.size());
    for (unsigned i = 8; i < u.size(); ++i) u[i] = uint8_t(i * 17 + 3);
    Bytes pseudo(12); put32(pseudo, 0, peer); put32(pseudo, 4, local); pseudo[9] = 17;
    put16(pseudo, 10, u.size()); pseudo.insert(pseudo.end(), u.begin(), u.end());
    if (!zeroChecksum) { auto c = checksum(pseudo); put16(u, 6, c ? c : 65535); }
    return ipPacket(peer, local, 17, u);
}
struct Event {
    uint32_t routerIp, sourceIp, destinationIp, sequence;
    uint16_t sourcePort, destinationPort, totalLength, identification, mtu;
    uint8_t protocol, kind, code, pointer;
    bool df;
    bool operator==(const Event&) const = default;
};
static Event expectedEvent(const Bytes& p) {
    return {get32(p, 12), get32(p, 40), get32(p, 44), get32(p, 52),
        get16(p, 48), get16(p, 50), get16(p, 30), get16(p, 32),
        uint16_t(p[20] == 3 && p[21] == 4 ? get16(p, 26) : 0),
        p[37], p[20], p[21], uint8_t(p[20] == 12 ? p[24] : 0), bool(p[34] & 0x40)};
}
struct ExpectedReply { Bytes original; uint8_t code; };
class Peer {
public:
    SIcmpErrorsHarness d;
    std::mt19937_64 rng{0x792119159272026ULL};
    std::deque<Event> events;
    std::deque<ExpectedReply> replies;
    unsigned cycles = 0, received = 0, transmitted = 0, rxDrops = 0, txDrops = 0, suppressed = 0, stalls = 0;
    bool eventReady = true, outReady = true, tick = false, randomReady = true;
    bool heldEvent = false, heldTx = false;
    Event held{};
    uint64_t heldLo = 0, heldHi = 0;
    unsigned heldKeep = 0, heldDestination = 0;
    bool heldLast = false;
    Bytes assembled;
    Peer() {
        d.set_io$$valid(0); d.set_io$$localIp(local); d.set_io$$netmask(0xffffff00);
        d.set_io$$generate(0); d.set_io$$code(2); d.set_io$$tick(0);
        d.set_io$$eventReady(0); d.set_io$$outReady(0);
        d.set_io$$lo(0); d.set_io$$hi(0); d.set_io$$keep(0); d.set_io$$last(0); d.set_io$$error(0);
        d.set_reset(1); d.step(); d.step(); d.set_reset(0);
    }
    Event outputEvent() {
        return {uint32_t(d.get_io$$routerIp()), uint32_t(d.get_io$$sourceIp()), uint32_t(d.get_io$$destinationIp()),
            uint32_t(d.get_io$$sequence()), uint16_t(d.get_io$$sourcePort()), uint16_t(d.get_io$$destinationPort()),
            uint16_t(d.get_io$$totalLength()), uint16_t(d.get_io$$identification()), uint16_t(d.get_io$$nextHopMtu()),
            uint8_t(d.get_io$$protocol()), uint8_t(d.get_io$$icmpType()), uint8_t(d.get_io$$icmpCode()),
            uint8_t(d.get_io$$pointer()), bool(d.get_io$$dontFragment())};
    }
    void step() {
        bool er = eventReady && (!randomReady || rng() % 4 != 0);
        bool tr = outReady && (!randomReady || rng() % 3 != 0);
        d.set_io$$eventReady(er); d.set_io$$outReady(tr); d.set_io$$tick(tick);
        d.step(); ++cycles;
        rxDrops += d.get_io$$rxDropped(); txDrops += d.get_io$$txDropped(); suppressed += d.get_io$$txSuppressed();
        if (heldEvent) check(d.get_io$$eventValid() && outputEvent() == held, "ICMP event unstable under stall");
        if (d.get_io$$eventValid()) {
            auto e = outputEvent();
            check(!events.empty() && e == events.front(), "ICMP event mismatch/unexpected event");
            if (er) { events.pop_front(); ++received; }
            else { held = e; ++stalls; }
        }
        heldEvent = d.get_io$$eventValid() && !er;
        if (heldTx) check(d.get_io$$outValid() && d.get_io$$outLo() == heldLo &&
            d.get_io$$outHi() == heldHi && d.get_io$$outKeep() == heldKeep &&
            d.get_io$$outLast() == heldLast && d.get_io$$outDestination() == heldDestination,
            "ICMP reply unstable under stall");
        if (d.get_io$$outValid()) {
            check(!replies.empty(), "unexpected ICMP reply");
            check(d.get_io$$outDestination() == get32(replies.front().original, 12), "ICMP routing destination mismatch");
            if (tr) {
                unsigned keep = d.get_io$$outKeep();
                check(keep && (keep & (keep + 1)) == 0, "ICMP reply invalid keep");
                unsigned count = __builtin_popcount(keep);
                check(d.get_io$$outLast() || count == 16, "ICMP reply partial nonfinal");
                for (unsigned i = 0; i < count; ++i) {
                    auto w = i < 8 ? d.get_io$$outLo() : d.get_io$$outHi();
                    assembled.push_back(uint8_t(w >> (8 * (i % 8))));
                }
                if (d.get_io$$outLast()) {
                    auto e = replies.front(); replies.pop_front();
                    unsigned quoteLength = std::min<unsigned>(28, get16(e.original, 2));
                    check(assembled.size() == 28 + quoteLength, "ICMP reply length");
                    check(assembled[0] == 0x45 && assembled[9] == 1 &&
                        get16(assembled, 2) == assembled.size() && get16(assembled, 6) == 0x4000,
                        "ICMP reply IP fields");
                    check(get32(assembled, 12) == local && get32(assembled, 16) == get32(e.original, 12), "ICMP reply IP tuple");
                    check(checksum(Bytes(assembled.begin(), assembled.begin() + 20)) == 0, "ICMP reply IPv4 checksum");
                    check(checksum(Bytes(assembled.begin() + 20, assembled.end())) == 0, "ICMP reply ICMP checksum");
                    check(assembled[20] == 3 && assembled[21] == e.code && get32(assembled, 24) == 0, "ICMP reply error fields");
                    check(std::equal(assembled.begin() + 28, assembled.end(), e.original.begin()), "ICMP quote bytes/padding mismatch");
                    assembled.clear(); ++transmitted;
                }
            } else {
                heldLo = d.get_io$$outLo(); heldHi = d.get_io$$outHi(); heldKeep = d.get_io$$outKeep();
                heldLast = d.get_io$$outLast(); heldDestination = d.get_io$$outDestination(); ++stalls;
            }
        }
        heldTx = d.get_io$$outValid() && !tr;
    }
    void idle(unsigned n = 80) { d.set_io$$valid(0); for (unsigned i = 0; i < n; ++i) step(); }
    void refill() { tick = true; idle(12); tick = false; }
    void send(const Bytes& p, bool generate = false, unsigned code = 2,
              bool badLast = false, bool sparse = false) {
        d.set_io$$generate(generate); d.set_io$$code(code);
        for (unsigned offset = 0; offset < p.size(); offset += 16) {
            unsigned n = std::min<unsigned>(16, p.size() - offset);
            uint64_t lo = 0, hi = 0;
            for (unsigned i = 0; i < n; ++i) (i < 8 ? lo : hi) |= uint64_t(p[offset + i]) << (8 * (i % 8));
            d.set_io$$lo(lo); d.set_io$$hi(hi); d.set_io$$keep(((1U << n) - 1) ^ (sparse && offset == 0 ? 2 : 0));
            d.set_io$$last(offset + n == p.size()); d.set_io$$error(badLast && offset + n == p.size());
            d.set_io$$valid(1);
            unsigned waited = 0;
            do { step(); check(++waited < 4000, "ICMP input deadlock"); } while (!d.get_io$$ready());
            d.set_io$$valid(0);
            if (rng() % 3 == 0) idle(1 + rng() % 4);
        }
        d.set_io$$valid(0);
    }
    void goodReceive(const Bytes& p) { events.push_back(expectedEvent(p)); send(p); idle(); check(events.empty(), "ICMP event missing"); }
    void badReceive(const Bytes& p, bool late = false, bool sparse = false) {
        unsigned before = received; send(p, false, 2, late, sparse); idle(); check(received == before, "malformed ICMP accepted");
    }
    void goodReply(const Bytes& p, unsigned code) {
        refill(); replies.push_back({p, uint8_t(code)}); send(p, true, code); idle(); check(replies.empty(), "ICMP reply missing");
    }
    void noReply(const Bytes& p, unsigned code = 2, bool late = false, bool sparse = false) {
        refill(); unsigned before = transmitted; send(p, true, code, late, sparse); idle();
        check(transmitted == before, "forbidden ICMP reply emitted");
    }
};
int main() { try {
    Peer p;
    for (unsigned i = 0; i < 120; ++i) {
        unsigned mode = i % 10;
        uint8_t kind = mode < 6 ? 3 : (mode < 8 ? 11 : 12);
        uint8_t code = kind == 3 ? (mode < 4 ? 4 : (mode == 4 ? 0 : 13)) : (mode & 1);
        uint16_t mtu[] = {0, 68, 576, 1492};
        auto q = quoted(i & 1 ? 6 : 17, i & 1 ? 1500 : 28, 0xfffffff0U + i);
        auto packet = errorPacket(q, kind, code, mtu[i % 4], i % 19);
        p.goodReceive(packet);
    }
    auto valid = errorPacket(quoted());
    { auto b = valid; b[10] ^= 1; p.badReceive(b); }
    { auto b = valid; b.back() ^= 1; p.badReceive(b); }
    { auto b = valid; put32(b, 16, local + 1); fixIp(b); p.badReceive(b); }
    { auto b = valid; b.resize(48); p.badReceive(b); }
    p.badReceive(valid, true); p.badReceive(valid, false, true);
    for (unsigned mode = 0; mode < 14; ++mode) {
        auto q = quoted();
        if (mode == 0) q[10] ^= 1;
        if (mode == 1) { put32(q, 12, local + 1); fixIp(q); }
        if (mode == 2) { put32(q, 16, 0xe0000001); fixIp(q); }
        if (mode == 3) { put16(q, 6, 0x2000); fixIp(q); }
        if (mode == 4) { put16(q, 6, 1); fixIp(q); }
        if (mode == 5) { put16(q, 6, 0x8000); fixIp(q); }
        if (mode == 6) { q[0] = 0x46; fixIp(q); }
        if (mode == 7) { put16(q, 2, 39); fixIp(q); }
        if (mode == 8) { q[9] = 1; fixIp(q); }
        if (mode == 9) q.resize(27);
        auto b = errorPacket(q, mode == 11 ? 11 : 3, mode == 10 ? 16 : (mode == 11 ? 2 : 4), 576,
            0, mode == 12 ? 0x7f000001 : (mode == 13 ? 0 : router));
        p.badReceive(b);
    }
    p.badReceive(errorPacket(quoted(), 12, 3));
    p.badReceive(errorPacket(quoted(), 3, 4, 576, 0, 0xc00002ff));
    { auto q = quoted(17, 28); put16(q, 24, 7); p.badReceive(errorPacket(q)); }
    { auto q = quoted(); put32(q, 16, 0xc00002ff); fixIp(q); p.badReceive(errorPacket(q)); }
    p.goodReceive(errorPacket(quoted(), 3, 4, 576, 0, 0xc63364ff));
    p.badReceive(errorPacket(quoted(), 5, 0)); // redirect ignored
    p.badReceive(ipPacket(router, local, 1, Bytes{8,0,0xf7,0xff,0,0,0,0})); // echo handled elsewhere
    // A stalled advisory consumer cannot block input; the third error is dropped.
    p.randomReady = false; p.eventReady = false;
    for (unsigned i = 0; i < 3; ++i) {
        auto b = errorPacket(quoted(6, 1500, 100 + i));
        if (i < 2) p.events.push_back(expectedEvent(b));
        p.send(b); p.idle();
    }
    p.eventReady = true; p.idle(); check(p.events.empty(), "ICMP event overflow/drain");
    p.randomReady = true;
    // Protocol errors quote exact original bytes, including short/odd payloads and excluding link padding.
    for (unsigned length : {0U,1U,7U,8U,9U,17U,127U,257U}) {
        auto b = ipPacket(peer, local, 99, Bytes(length, 0xa7));
        b.resize(b.size() + 19, 0x65); p.goodReply(b, 2);
    }
    p.goodReply(ipPacket(0xc63364ff, local, 99, Bytes(8)), 2); // unrelated off-link suffix is not local broadcast
    p.d.set_io$$netmask(0xfffffffe);
    p.goodReply(ipPacket(local + 1, local, 99, Bytes(8)), 2); // /31 point-to-point
    p.d.set_io$$netmask(0xffffff00);
    for (unsigned length : {0U,1U,2U,15U,16U,17U,255U}) {
        for (bool noChecksum : {false, true}) {
            auto b = udpPacket(length, noChecksum); b.resize(b.size() + 13, 0x8e); p.goodReply(b, 3);
        }
    }
    { auto b = udpPacket(15); b.back() ^= 1; p.noReply(b, 3); }
    { auto b = udpPacket(15); put16(b, 24, 8); p.noReply(b, 3); }
    { auto b = udpPacket(15, true); put16(b, 24, 7); p.noReply(b, 3); }
    { auto b = udpPacket(15); b.resize(30); p.noReply(b, 3); }
    { auto b = udpPacket(15); b[10] ^= 1; p.noReply(b, 3); }
    p.noReply(udpPacket(15), 3, true); p.noReply(udpPacket(15), 3, false, true);
    for (uint32_t source : {0U,0x7f000001U,0xe0000001U,0xf0000001U,0xffffffffU,0xc00002ffU})
        p.noReply(ipPacket(source, local, 99, Bytes(8)));
    for (uint32_t destination : {0xffffffffU,0xe0000001U,0xc00002ffU,local + 1})
        p.noReply(ipPacket(peer, destination, 99, Bytes(8)));
    for (unsigned type : {3U,4U,5U,11U,12U}) {
        Bytes payload(8); payload[0] = type; p.noReply(ipPacket(peer, local, 1, payload));
    }
    for (unsigned flags : {1U,0x2000U,0x8000U}) {
        auto b = ipPacket(peer, local, 99, Bytes(8)); put16(b, 6, flags); fixIp(b); p.noReply(b);
    }
    p.noReply(ipPacket(peer, local, 99, Bytes(8)), 3);
    p.noReply(ipPacket(peer, local, 1, Bytes(8)), 2);
    p.noReply(ipPacket(peer, local, 6, Bytes(20)), 2);
    p.noReply(udpPacket(15), 2);
    p.noReply(ipPacket(peer, local, 99, Bytes(8)), 4);
    // Rate limiting does not stall input, and replenishment admits another reply.
    auto rejected = ipPacket(peer, local, 99, Bytes(8));
    p.goodReply(rejected, 2);
    unsigned oldSuppressed = p.suppressed;
    p.send(rejected, true); p.idle();
    check(p.suppressed == oldSuppressed + 1, "ICMP rate limit missing");
    p.goodReply(rejected, 2);
    // One active output plus two queued requests; a fourth is dropped, no deadlock.
    p.randomReady = false; p.outReady = false;
    for (unsigned i = 0; i < 4; ++i) {
        auto b = rejected; put16(b, 4, i); fixIp(b);
        p.refill();
        if (i < 3) p.replies.push_back({b, 2});
        p.send(b, true); p.idle();
    }
    p.outReady = true; p.idle(); check(p.replies.empty(), "ICMP TX overflow/drain");
    check(p.rxDrops >= 20 && p.txDrops >= 20 && p.suppressed >= 2 && p.stalls > 0, "ICMP negative coverage");
    std::cout << "ICMP_ERRORS_PASS received=" << p.received << " replies=" << p.transmitted
        << " rx_drops=" << p.rxDrops << " tx_drops=" << p.txDrops << " suppressed=" << p.suppressed
        << " cycles=" << p.cycles << " stalls=" << p.stalls << '\n';
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
