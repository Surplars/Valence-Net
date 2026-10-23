// Standalone service evidence. This bench intentionally lives outside sim/ so
// an already-running source manifest regression remains unchanged.
// Geometry: existing MultiEthernetTcpHarness, 4 contexts, 4 TX banks and 2 KiB
// RX ring per context. Interface carries Ethernet frames without preamble/FCS.
// Deadlines include actual Ethernet padding, FCS, preamble and IFG wire cost.
#include "MultiEthernetTcpHarness.h"
#include "multi_tcp_peer.h"

using Peer = MultiTcpPeer<SMultiEthernetTcpHarness, true>;

static void receiveLoad(unsigned active, unsigned payload) {
    Peer p;
    p.random = false;
    for (unsigned c = 0; c < active; ++c) {
        p.learn(c);
        p.active(c, 100000 + c * 10000, 200000 + c * 10000, 1460);
    }
    const unsigned count = 256;
    const auto initialReceived = p.receivedBytes;
    const unsigned initialDrops = p.drops, initialBusy = p.busyDrops;
    const unsigned begin = p.cycles;
    // 84 wire bytes for a minimum frame -> 10.5 clocks at 156.25 MHz/10G.
    // 1538 wire bytes for a 1460-byte TCP payload -> 192.25 clocks.
    const unsigned numerator = payload == 1460 ? 769 : 21;
    const unsigned denominator = payload == 1460 ? 4 : 2;
    for (unsigned n = 0; n < count; ++n) {
        const unsigned c = n % active;
        const unsigned deadline = begin + ((n + 1) * numerator) / denominator;
        auto bytes = p.pattern(payload);
        const auto sequence = p.peerNext[c];
        p.expect(c, bytes);
        p.peerNext[c] += payload;
        // wire() wraps the IPv4 packet in a complete Ethernet frame and pads
        // the input to 60 octets; every 128-bit MAC-input beat is handshaken.
        p.wire(p.encode(c, sequence, p.planned[c], payload ? 24 : 16, 65535, bytes), false, c);
        if (p.cycles > deadline) {
            std::cerr << "deadline active=" << active << " payload=" << payload
                      << " packet=" << n << " cycles=" << p.cycles - begin
                      << " allowed=" << deadline - begin << '\n';
            throw std::runtime_error("MAC input backpressure missed the 10G offered-load deadline");
        }
        while (p.cycles < deadline) p.step();
    }
    const unsigned elapsed = p.cycles - begin;
    check(elapsed == (count * numerator) / denominator, "offered load was stretched below 10G");
    p.idle(3000);
    p.drain();
    if (p.drops != initialDrops || p.busyDrops != initialBusy) {
        unsigned pendingBytes=0, receivedBytes=0;
        for(unsigned c=0;c<4;++c){pendingBytes+=p.expectedRx[c].size();receivedBytes+=p.receivedBytes[c]-initialReceived[c];}
        std::cerr<<"RX overload active="<<active<<" payload="<<payload<<" totalDrops="<<p.drops-initialDrops
                 <<" busyDrops="<<p.busyDrops-initialBusy<<" delivered="<<receivedBytes<<" pending="<<pendingBytes<<'\n';
    }
    check(p.drops == initialDrops, "complete Ethernet RX path dropped a packet at 10G");
    check(p.busyDrops == initialBusy, "TCP context admission overflowed at 10G");
    unsigned delivered = 0;
    for (unsigned c = 0; c < 4; ++c) {
        check(p.expectedRx[c].empty(), "application stream did not deliver every offered byte");
        delivered += p.receivedBytes[c] - initialReceived[c];
        if (c < active) {
            p.select(c);
            check(p.state(c) == 4, "connection changed state during RX service");
            check(p.d.get_io$$receiveNext() == p.peerNext[c], "TCP receive sequence lost an offered payload");
        } else {
            check(p.state(c) == 0, "idle connection acquired unexpected receive ownership");
        }
    }
    check(delivered == count * payload, "incorrect total delivered byte count");
    std::cout << "MAC RX " << (payload ? std::to_string(payload) + "B data" : "pure ACK")
              << ", " << active << " active: " << elapsed << "/" << count
              << " = " << double(elapsed) / count << " cycles/frame; zero drops; "
              << delivered << " application bytes verified; 10G deadlines all met\n";
}

int main() {
    unsigned failed=0;
    for(auto test:std::vector<std::pair<unsigned,unsigned>>{{1,0},{4,0},{1,1},{4,1},{4,1460}}){
        try { receiveLoad(test.first,test.second); }
        catch (const std::exception &e) {
            ++failed;
            std::cerr << "multi_rx_service case("<<test.first<<","<<test.second<<"): " << e.what() << '\n';
        }
    }
    if(!failed)std::cout << "complete Ethernet RX 10G service checks passed (2 KiB RX/context)\n";
    return failed?1:0;
}
