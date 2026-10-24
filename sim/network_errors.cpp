#include "NetworkErrorsHarness.h"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
using Bytes = std::vector<uint8_t>;
static void check(bool b, const std::string& s) { if (!b) throw std::runtime_error(s); }
static uint16_t get16(const Bytes& p, unsigned i) { return (unsigned(p.at(i)) << 8) | p.at(i + 1); }
static uint32_t get32(const Bytes& p, unsigned i) { return (uint32_t(get16(p, i)) << 16) | get16(p, i + 2); }
static uint16_t checksum(const Bytes& p) {
    uint32_t s = 0;
    for (unsigned i = 0; i < p.size(); i += 2) s += (unsigned(p[i]) << 8) | (i + 1 < p.size() ? p[i + 1] : 0);
    while (s >> 16) s = (s & 65535) + (s >> 16);
    return ~s;
}
static void put16(Bytes& p, unsigned i, unsigned n) { p.at(i) = n >> 8; p.at(i + 1) = n; }
static void put32(Bytes& p, unsigned i, uint32_t n) { put16(p, i, n >> 16); put16(p, i + 2, n); }
static constexpr uint32_t local = 0xc0000202, remote = 0xc6336407, router = 0xc0000201;
struct Error {
    uint32_t routerIp = router, source = local, destination = remote, sequence = 100;
    unsigned protocol = 6, sport = 12345, dport = 8080, total = 1500, identification = 123;
    bool df = true; unsigned mtu = 1000, kind = 3, code = 4, pointer = 0;
};
static Error udpError(const Bytes& p, unsigned mtu = 1000) {
    Error e;
    e.source = get32(p,12); e.destination = get32(p,16); e.protocol = 17;
    e.sport = get16(p,20); e.dport = get16(p,22); e.sequence = get32(p,24);
    e.total = get16(p,2); e.identification = get16(p,4); e.df = get16(p,6) & 0x4000; e.mtu = mtu;
    return e;
}
struct Update { unsigned mtu; uint32_t generation; bool increase; };
struct TcpNotice { uint32_t generation, sequence; unsigned mtu, kind, code; };
struct UdpNotice { bool local; uint32_t destination; unsigned sport, dport, length, mtu, kind, code; };
class Peer {
public:
    SNetworkErrorsHarness d;
    std::mt19937_64 rng{0x1191592720261023ULL};
    unsigned cycles = 0, stalls = 0, drops = 0;
    unsigned tcpMtu = 1500, tcpLimit = 1500, tcpState = 4, udpMtu = 1500;
    uint32_t generation = 1, oldest = 100, actual = 200, reserved = 200;
    bool tcpTick = false, tcpClear = false, udpTick = false, udpFlush = false;
    bool outReady = true, randomReady = true, held = false;
    uint64_t heldLo = 0, heldHi = 0; unsigned heldKeep = 0; bool heldLast = false;
    std::vector<Update> updates;
    std::vector<TcpNotice> tcpNotices;
    std::vector<UdpNotice> udpNotices;
    std::deque<Bytes> frames;
    Bytes assembly;
    Peer() {
        d.set_io$$errorValid(0); error(Error{});
        d.set_io$$tcpTuple$$localIp(local); d.set_io$$tcpTuple$$remoteIp(remote);
        d.set_io$$tcpTuple$$localPort(12345); d.set_io$$tcpTuple$$remotePort(8080);
        d.set_io$$commandValid(0); d.set_io$$inValid(0); d.set_io$$routedValid(0); d.set_io$$routedSuccess(0);
        d.set_io$$inLo(0); d.set_io$$inHi(0); d.set_io$$inKeep(0); d.set_io$$inLast(0); d.set_io$$inError(0);
        d.set_io$$command$$sourceIp(local); d.set_io$$command$$destinationIp(remote);
        d.set_io$$command$$sourcePort(12345); d.set_io$$command$$destinationPort(8080);
        d.set_io$$command$$length(0); d.set_io$$command$$identification(0); d.set_io$$command$$ttl(64); d.set_io$$command$$tos(0);
        drive(); d.set_reset(1); d.step(); d.step(); d.set_reset(0);
        tcpClear = true; step(); tcpClear = false; idle(3);
    }
    void drive() {
        d.set_io$$tcpState(tcpState); d.set_io$$tcpGeneration(generation);
        d.set_io$$tcpOldest(oldest); d.set_io$$tcpActual(actual); d.set_io$$tcpReserved(reserved);
        d.set_io$$tcpMtu(tcpMtu); d.set_io$$tcpLimit(tcpLimit);
        d.set_io$$tcpTick(tcpTick); d.set_io$$tcpClear(tcpClear);
        d.set_io$$udpMtu(udpMtu); d.set_io$$udpTick(udpTick); d.set_io$$udpFlush(udpFlush);
    }
    void error(const Error& e) {
        d.set_io$$error$$routerIp(e.routerIp); d.set_io$$error$$sourceIp(e.source); d.set_io$$error$$destinationIp(e.destination);
        d.set_io$$error$$protocol(e.protocol); d.set_io$$error$$sourcePort(e.sport); d.set_io$$error$$destinationPort(e.dport);
        d.set_io$$error$$sequence(e.sequence); d.set_io$$error$$totalLength(e.total); d.set_io$$error$$identification(e.identification);
        d.set_io$$error$$dontFragment(e.df); d.set_io$$error$$nextHopMtu(e.mtu);
        d.set_io$$error$$icmpType(e.kind); d.set_io$$error$$icmpCode(e.code); d.set_io$$error$$pointer(e.pointer);
    }
    void step() {
        drive();
        bool ready = outReady && (!randomReady || rng() % 4 != 0);
        d.set_io$$outReady(ready);
        d.step(); ++cycles;
        if (d.get_io$$updateValid()) {
            Update u{unsigned(d.get_io$$updateMtu()), uint32_t(d.get_io$$updateGeneration()), bool(d.get_io$$updateIncrease())};
            check(u.generation == generation && !tcpClear, "stale TCP PMTU update");
            updates.push_back(u); tcpMtu = u.mtu;
        }
        if (d.get_io$$tcpNoticeValid()) {
            check(!tcpClear && d.get_io$$tcpNotice$$generation() == generation, "stale TCP ICMP notice");
            tcpNotices.push_back({uint32_t(d.get_io$$tcpNotice$$generation()), uint32_t(d.get_io$$tcpNotice$$sequence()),
                unsigned(d.get_io$$tcpNotice$$mtu()), unsigned(d.get_io$$tcpNotice$$icmpType()), unsigned(d.get_io$$tcpNotice$$icmpCode())});
        }
        if (d.get_io$$udpNoticeValid()) {
            check(!udpFlush, "UDP ICMP notice leaked through flush");
            udpNotices.push_back({bool(d.get_io$$udpNotice$$local()), uint32_t(d.get_io$$udpNotice$$destinationIp()),
                unsigned(d.get_io$$udpNotice$$sourcePort()), unsigned(d.get_io$$udpNotice$$destinationPort()),
                unsigned(d.get_io$$udpNotice$$length()), unsigned(d.get_io$$udpNotice$$mtu()),
                unsigned(d.get_io$$udpNotice$$icmpType()), unsigned(d.get_io$$udpNotice$$icmpCode())});
        }
        drops += d.get_io$$udpDropped();
        if (held) check(d.get_io$$outValid() && d.get_io$$outLo() == heldLo && d.get_io$$outHi() == heldHi &&
            d.get_io$$outKeep() == heldKeep && bool(d.get_io$$outLast()) == heldLast, "UDP path output changed while stalled");
        if (d.get_io$$outValid()) {
            if (ready) {
                unsigned keep = d.get_io$$outKeep();
                check(keep && (keep & (keep + 1)) == 0, "UDP path keep");
                unsigned count = __builtin_popcount(keep);
                check(d.get_io$$outLast() || count == 16, "UDP partial nonfinal");
                for (unsigned i = 0; i < count; ++i) {
                    uint64_t word = i < 8 ? d.get_io$$outLo() : d.get_io$$outHi();
                    assembly.push_back(uint8_t(word >> (8 * (i % 8))));
                }
                if (d.get_io$$outLast()) {
                    check(assembly.size() >= 28 && assembly.size() == get16(assembly,2), "UDP serialized length");
                    check(d.get_io$$outDestination() == get32(assembly,16), "UDP destination metadata");
                    check(checksum(Bytes(assembly.begin(),assembly.begin()+20)) == 0, "UDP IP checksum");
                    Bytes pseudo(12); put32(pseudo,0,get32(assembly,12)); put32(pseudo,4,get32(assembly,16));
                    pseudo[9] = 17; put16(pseudo,10,assembly.size()-20);
                    pseudo.insert(pseudo.end(),assembly.begin()+20,assembly.end());
                    check(checksum(pseudo) == 0, "UDP transport checksum");
                    frames.push_back(assembly); assembly.clear();
                }
            } else {
                heldLo = d.get_io$$outLo(); heldHi = d.get_io$$outHi(); heldKeep = d.get_io$$outKeep(); heldLast = d.get_io$$outLast(); ++stalls;
            }
        }
        held = d.get_io$$outValid() && !ready;
    }
    void idle(unsigned n = 8) { for (unsigned i=0;i<n;++i) step(); }
    void inject(const Error& e) { error(e); d.set_io$$errorValid(1); step(); d.set_io$$errorValid(0); }
    void freshTcp(unsigned mtu = 1500, unsigned limit = 1500) {
        tcpMtu = mtu; tcpLimit = limit; tcpState = 4; generation++;
        oldest = 100; actual = 200; reserved = 200; tcpTick = false;
        tcpClear = true; step(); tcpClear = false; idle(3);
    }
    void tcpAccept(const Error& e, unsigned mtu) {
        auto u = updates.size(), n = tcpNotices.size(); inject(e); idle();
        check(updates.size() == u + 1 && tcpNotices.size() == n + 1 && tcpMtu == mtu &&
            !updates.back().increase, "TCP PMTU decrease missing/wrong");
    }
    void tcpReject(const Error& e) {
        auto u = updates.size(), n = tcpNotices.size(); inject(e); idle();
        check(updates.size() == u && tcpNotices.size() == n, "invalid TCP ICMP quote accepted");
    }
    void route(bool success, bool withTick = false) {
        udpTick = withTick; d.set_io$$routedSuccess(success); d.set_io$$routedValid(1); step();
        d.set_io$$routedValid(0); udpTick = false; idle(3);
    }
    Bytes sendUdp(unsigned n, uint32_t destination, unsigned id, bool expectWire = true,
                  bool badLast = false, int declared = -1) {
        check(frames.empty(), "undrained UDP frame in peer");
        d.set_io$$command$$destinationIp(destination); d.set_io$$command$$identification(id);
        d.set_io$$command$$length(declared < 0 ? n : unsigned(declared));
        d.set_io$$commandValid(1);
        unsigned count = 0;
        do { step(); check(++count < 4000,"UDP command deadlock"); } while (!d.get_io$$commandReady());
        d.set_io$$commandValid(0);
        Bytes payload(n); for (unsigned i=0;i<n;++i) payload[i] = uint8_t(i*29+id);
        for (unsigned offset=0;offset<std::max(1U,n);offset+=16) {
            unsigned take = std::min(16U,n-offset); uint64_t lo=0,hi=0;
            for (unsigned i=0;i<take;++i) (i<8?lo:hi) |= uint64_t(payload[offset+i]) << (8*(i%8));
            d.set_io$$inLo(lo); d.set_io$$inHi(hi); d.set_io$$inKeep((1U<<take)-1);
            d.set_io$$inLast(offset+take==n); d.set_io$$inError(badLast && offset+take==n); d.set_io$$inValid(1);
            count=0; do { step(); check(++count<4000,"UDP payload deadlock"); } while(!d.get_io$$inReady());
            d.set_io$$inValid(0);
        }
        if (expectWire) {
            for (unsigned i=0;frames.empty() && i<4000;++i) step();
            check(!frames.empty(),"UDP packet missing"); Bytes packet=frames.front(); frames.pop_front();
            check(packet.size()==n+28 && get32(packet,16)==destination && get16(packet,4)==id, "UDP command/length mismatch");
            check(std::equal(packet.begin()+28,packet.end(),payload.begin()),"UDP path payload mismatch");
            idle(3); return packet;
        }
        idle(120); check(frames.empty(),"rejected UDP datagram escaped"); return {};
    }
    void flushUdp() { udpFlush=true;step();udpFlush=false;idle(3); }
    void udpReject(const Error& e) {
        auto n=udpNotices.size();inject(e);idle();check(udpNotices.size()==n,"unmatched UDP ICMP quote accepted");
    }
    void udpAccept(const Error& e, unsigned mtu) {
        auto n=udpNotices.size();inject(e);idle();
        check(udpNotices.size()==n+1 && !udpNotices.back().local && udpNotices.back().mtu==mtu &&
            udpNotices.back().destination==e.destination,"UDP ICMP notice/decrease wrong");
    }
    void localReject(unsigned n, uint32_t destination, unsigned id, unsigned mtu) {
        auto notes=udpNotices.size(); unsigned beforeDrops=drops;sendUdp(n,destination,id,false);
        check(drops==beforeDrops+1 && udpNotices.size()==notes+1 && udpNotices.back().local &&
            udpNotices.back().mtu==mtu && udpNotices.back().length==n,"UDP local PMTU rejection/notice");
    }
};
int main(){try{
    Peer p; Error e;
    p.tcpAccept(e,1000);
    p.tcpReject(e); // no redundant decrease
    for(unsigned mode=0;mode<13;++mode){
        p.freshTcp(); Error q;
        if(mode==0)q.source++;
        if(mode==1)q.destination++;
        if(mode==2)q.sport++;
        if(mode==3)q.dport++;
        if(mode==4)q.protocol=17;
        if(mode==5)q.sequence=99;
        if(mode==6)q.sequence=200;
        if(mode==7){q.sequence=250;p.reserved=300;}
        if(mode==8)q.df=false;
        if(mode==9)q.mtu=67;
        if(mode==10)q.mtu=1600;
        if(mode==11)q.total=900;
        if(mode==12)p.actual=p.oldest;
        p.tcpReject(q);
    }
    for(unsigned state:{0U,1U,10U}){p.freshTcp();p.tcpState=state;p.tcpReject(e);}
    p.freshTcp();p.oldest=0xfffffff0;p.actual=0x20;p.reserved=0x40;e.sequence=0x10;
    p.tcpAccept(e,1000);
    e.sequence=0x20;p.tcpReject(e);e.sequence=0xffffffef;p.tcpReject(e);e.sequence=100;
    // Candidate validation sees current ACK/tuple/epoch, not stale captured TCB values.
    for(unsigned mode=0;mode<4;++mode){
        p.freshTcp();auto updates=p.updates.size(),notes=p.tcpNotices.size();p.inject(e);
        if(mode==0)p.oldest=p.actual;
        if(mode==1)p.generation++;
        if(mode==2)p.tcpClear=true;
        if(mode==3)p.d.set_io$$tcpTuple$$remotePort(8081);
        p.idle();check(p.updates.size()==updates&&p.tcpNotices.size()==notes,"TCP validation race");
        p.tcpClear=false;p.d.set_io$$tcpTuple$$remotePort(8080);
    }
    p.freshTcp();{auto before=p.updates.size();p.inject(e);p.step();p.generation++;p.idle();check(p.updates.size()==before,"TCP generation publication race");}
    // Zero-MTU fallback selects the largest strictly lower standard plateau.
    for(auto pair:std::vector<std::pair<unsigned,unsigned>>{{1500,1492},{1492,1006},{1006,508},{508,296},{296,68}}){
        p.freshTcp(pair.first,1500);Error q;q.mtu=0;q.total=pair.first;p.tcpAccept(q,pair.second);
    }
    // Duplicate zero-MTU reports for one datagram are idempotent. Only a
    // newly smaller quoted datagram can advance the plateau search.
    p.freshTcp();{Error original;original.mtu=0;
        p.tcpAccept(original,1492);
        for(unsigned i=0;i<4;++i)p.tcpReject(original);
        Error smaller=original;smaller.total=1492;p.tcpAccept(smaller,1006);
        p.tcpReject(original);p.tcpReject(smaller);
        smaller.total=1006;p.tcpAccept(smaller,508);p.tcpReject(smaller);
    }
    p.freshTcp(68,1500);{Error q;q.mtu=0;q.total=68;p.tcpReject(q);}
    p.freshTcp();{Error q;q.kind=11;q.code=0;auto u=p.updates.size(),n=p.tcpNotices.size();p.inject(q);p.idle();
        check(p.updates.size()==u&&p.tcpNotices.size()==n+1&&p.tcpNotices.back().kind==11,"TCP soft error missing");}
    // Expiry waits for no wire or reserved flight; a late reservation postpones publication without restarting age.
    p.freshTcp();p.tcpAccept(Error{},1000);auto beforeExpiry=p.updates.size();
    p.tcpTick=true;p.idle(301);p.tcpTick=false;
    check(p.tcpMtu==1000&&p.updates.size()==beforeExpiry,"busy TCP PMTU expired");
    p.actual=p.oldest;p.reserved=p.oldest;p.step();
    p.reserved=p.oldest+64;p.step();p.idle(5);
    check(p.tcpMtu==1000&&p.updates.size()==beforeExpiry,"late reservation bypassed idle-only expiry");
    p.reserved=p.oldest;p.idle(8);
    check(p.tcpMtu==1500&&p.updates.size()>beforeExpiry&&p.updates.back().increase,"TCP expiry did not retry after idle");
    p.tcpState=0;

    // No successful routed record means no learning.
    auto a=p.sendUdp(1400,remote,10);Error ua=udpError(a,1000);
    p.udpReject(ua);p.route(false);p.udpReject(ua);
    a=p.sendUdp(1400,remote,11);p.route(true);ua=udpError(a,1000);
    for(unsigned mode=0;mode<9;++mode){
        auto q=ua;
        if(mode==0)q.source++;
        if(mode==1)q.destination++;
        if(mode==2)q.sport++;
        if(mode==3)q.dport++;
        if(mode==4)q.identification++;
        if(mode==5)q.total--;
        if(mode==6)q.sequence^=1;
        if(mode==7)q.protocol=6;
        if(mode==8)q.df=false;
        p.udpReject(q);
    }
    p.udpAccept(ua,1000);p.udpReject(ua);
    {auto q=ua;q.mtu=1492;p.udpReject(q);}
    {auto q=ua;q.mtu=67;p.udpReject(q);}
    p.localReject(1000,remote,12,1000);
    a=p.sendUdp(972,remote,13);p.route(true); // exact 1000-byte IPv4 datagram
    auto b=p.sendUdp(1400,remote+1,14);p.route(true); // different destination unaffected
    auto ub=udpError(b,0);p.udpAccept(ub,1006);
    for(unsigned i=0;i<4;++i)p.udpReject(ub);
    p.localReject(1000,remote+1,15,1006);
    {auto q=udpError(b,1000);q.kind=3;q.code=3;p.udpAccept(q,1006);}
    // A genuinely smaller, successfully routed UDP datagram supplies the next
    // plateau; its repeated quote and the older large quote cannot lower again.
    auto smallerUdp=p.sendUdp(978,remote+1,19);p.route(true);
    auto smallerQuote=udpError(smallerUdp,0);p.udpAccept(smallerQuote,508);
    p.udpReject(ub);p.udpReject(smallerQuote);
    p.localReject(500,remote+1,20,508);
    // Old routed records are evicted; expired ICMP identities cannot lower a later path.
    p.udpReject(ua);
    p.udpTick=true;p.idle(60);p.udpTick=false;p.udpReject(udpError(b,800));
    // PMTU ages independently, restoring the link ceiling after 600 seconds.
    p.udpTick=true;p.idle(540);p.udpTick=false;
    a=p.sendUdp(1400,remote,16);p.route(true);
    // Flush drops a queued advisory and all identities/cache state.
    ua=udpError(a,1000);p.inject(ua);p.step();p.udpFlush=true;p.step();p.udpFlush=false;p.idle();
    p.udpReject(ua);
    a=p.sendUdp(1400,remote,17);p.route(true);ua=udpError(a,1000);p.udpAccept(ua,1000);
    p.flushUdp();a=p.sendUdp(1400,remote,18);p.route(true);
    // Two-entry history replacement exactly on an old entry's expiry tick.
    p.flushUdp();auto h0=p.sendUdp(1400,remote,30);p.route(true);
    auto h1=p.sendUdp(1400,remote+1,31);p.route(true);
    p.udpTick=true;p.idle(59);p.udpTick=false;
    auto h2=p.sendUdp(1400,remote+2,32);p.route(true,true);
    p.udpAccept(udpError(h2,900),900);
    p.udpReject(udpError(h0,800));p.udpReject(udpError(h1,800));
    // Failed serializer input cannot create a quoted identity.
    p.flushUdp();unsigned badDrops=p.drops;
    p.sendUdp(16,remote,50,false,true);check(p.drops==badDrops+1,"UDP corrupt payload not dropped");
    p.route(true); // spurious result has no submitted packet
    Error fake;fake.protocol=17;fake.total=44;fake.identification=50;fake.sequence=(24U<<16)|1;fake.mtu=68;p.udpReject(fake);
    badDrops=p.drops;p.sendUdp(16,remote,52,false,false,17);
    check(p.drops==badDrops+1,"UDP incomplete payload not dropped");
    auto empty=p.sendUdp(0,remote,53);p.route(true);
    auto emptyError=udpError(empty);emptyError.code=3;p.udpAccept(emptyError,1500);
    auto odd=p.sendUdp(1,remote+1,54);p.route(true);
    auto oddError=udpError(odd);oddError.kind=11;oddError.code=0;p.udpAccept(oddError,1500);
    p.localReject(65535,remote,51,1500);
    check(p.stalls>0&&p.drops>=5,"network error coverage");
    std::cout<<"NETWORK_ERRORS_PASS tcp_updates="<<p.updates.size()<<" tcp_notices="<<p.tcpNotices.size()
        <<" udp_notices="<<p.udpNotices.size()<<" drops="<<p.drops<<" cycles="<<p.cycles<<" stalls="<<p.stalls<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
