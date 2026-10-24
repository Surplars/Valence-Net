#include "MultiTcpEndpointHarness.h"
#include "multi_tcp_peer.h"

// Capture the independent stateless channel even inside MultiTcpPeer's helpers.
// It has no connection descriptor and must never consume a completion-ledger slot.
struct ExceptionModel : SMultiTcpEndpointHarness {
    bool resetActive=false, ready=false;
    std::vector<std::vector<uint8_t>> exceptions;
    std::vector<uint8_t> assembling;
    using ExceptionBeat=std::tuple<uint64_t,uint64_t,unsigned,bool,unsigned>;
    std::optional<ExceptionBeat> heldException;
    void set_reset(uint64_t value){resetActive=value;SMultiTcpEndpointHarness::set_reset(value);}
    void set_io$$exceptionReady(uint64_t value){ready=value;SMultiTcpEndpointHarness::set_io$$exceptionReady(value);}
    void step(){
        SMultiTcpEndpointHarness::step();
        if(resetActive){heldException.reset();assembling.clear();return;}
        ExceptionBeat beat{get_io$$exceptionLo(),get_io$$exceptionHi(),
            unsigned(get_io$$exceptionKeep()),bool(get_io$$exceptionLast()),unsigned(get_io$$exceptionDestination())};
        if(heldException)check(get_io$$exceptionValid()&&beat==*heldException,"exception frame/route changed while blocked");
        heldException=get_io$$exceptionValid()&&!ready?std::optional<ExceptionBeat>(beat):std::nullopt;
        if(get_io$$exceptionValid()&&ready){
            unsigned keep=get_io$$exceptionKeep();check(keep&&(keep&(keep+1))==0,"exception keep");
            for(unsigned i=0;i<unsigned(__builtin_popcount(keep));++i)
                assembling.push_back((i<8?get_io$$exceptionLo():get_io$$exceptionHi())>>(8*(i%8)));
            if(get_io$$exceptionLast()){
                check(assembling.size()==40&&u16(assembling,2)==40&&assembling[9]==6,"exception framing");
                check(sum({assembling.begin(),assembling.begin()+20})==0,"exception IP checksum");
                std::vector<uint8_t> pseudo(assembling.begin()+12,assembling.begin()+20);
                pseudo.insert(pseudo.end(),{0,6,0,20});
                pseudo.insert(pseudo.end(),assembling.begin()+20,assembling.end());
                check(sum(pseudo)==0,"exception TCP checksum");
                check(u32(assembling,16)==get_io$$exceptionDestination(),"exception destination");
                exceptions.push_back(assembling);assembling.clear();
            }
        }
    }
};
using Peer=MultiTcpPeer<ExceptionModel>;
static void repairTcp(std::vector<uint8_t>&v){
    put16(v,36,0);std::vector<uint8_t>pseudo(v.begin()+12,v.begin()+20);
    unsigned len=u16(v,2)-20;pseudo.push_back(0);pseudo.push_back(6);pseudo.push_back(len>>8);pseudo.push_back(len);
    pseudo.insert(pseudo.end(),v.begin()+20,v.begin()+u16(v,2));put16(v,36,sum(pseudo));
}
static std::vector<uint8_t> unknown(Peer&p,uint32_t seq,unsigned flags=2,uint32_t ack=0){
    auto v=p.encode(0,seq,ack,flags,65535);put16(v,22,6000);repairTcp(v);return v;
}
static void refill(Peer&p){p.idle(5,true);p.idle(60);}

static void unknownAndListener(){
    Peer p;p.random=false;p.d.set_io$$exceptionReady(1);
    p.wire(unknown(p,0xffffffffU));p.idle(200);
    check(p.d.exceptions.size()==1&&p.d.exceptions.back()[33]==20&&u32(p.d.exceptions.back(),28)==0,
        "unknown SYN reset or sequence wrap");
    check(p.d.get_io$$outstanding()==0&&p.pending.empty()&&p.packets.empty(),"stateless reset entered connection ledger");
    refill(p);p.wire(unknown(p,31,16,0x87654321));p.idle(200);
    check(p.d.exceptions.size()==2&&p.d.exceptions.back()[33]==4&&u32(p.d.exceptions.back(),24)==0x87654321,
        "unknown ACK reset");
    refill(p);p.wire(unknown(p,32,4));p.idle(160);
    check(p.d.exceptions.size()==2,"RST generated an RST");
    auto malformed=unknown(p,33);malformed[36]^=1;p.wire(malformed);p.idle(160);
    check(p.d.exceptions.size()==2,"invalid TCP checksum reset");
    malformed=unknown(p,34);malformed[10]^=1;p.wire(malformed);p.idle(160);
    check(p.d.exceptions.size()==2,"invalid IP checksum reset");
    p.open(0,10000,true,512);
    refill(p);auto first=p.d.exceptions.size();
    p.wire(p.encode(0,90000,0,0));p.idle(200);
    check(p.d.exceptions.size()==first&&p.state(0)==1,"LISTEN answered non-SYN/non-ACK");
    p.wire(p.encode(0,90000,777,16));p.idle(200);
    check(p.d.exceptions.size()==first+1&&u32(p.d.exceptions.back(),24)==777&&p.state(0)==1,
        "LISTEN unacceptable ACK must reset without allocating tuple");
    p.peerNext[0]=90001;p.rx(0,90000,0,2);p.drain();
    check(p.state(0)==3,"LISTEN normal SYN disabled by exception path");
    p.rx(0,p.peerNext[0],p.planned[0]);check(p.state(0)==4,"normal passive handshake failed");
}

static void blockedIsolation(){
    Peer p;p.random=false;p.d.set_io$$exceptionReady(0);
    p.active(0,10000,20000,512);p.active(1,30000,40000,512);
    // Fill exception storage and leave the first response offered indefinitely.
    for(unsigned n=0;n<16;++n){refill(p);p.wire(unknown(p,50000+n));p.idle(100);}
    check(p.d.get_io$$exceptionValid(),"exception stall not exercised");
    auto before=p.d.exceptions.size();auto busy=p.busyDrops;
    p.d.set_io$$noticeReadyMask(14);
    p.rx(0,p.peerNext[0],p.planned[0],17);++p.peerNext[0];
    for(unsigned n=0;n<16;++n)p.wire(p.encode(0,p.peerNext[0],p.planned[0]));
    p.receive(1,p.pattern(137));p.idle(400);
    check(p.busyDrops>busy&&p.expectedRx[1].empty(),"busy context or blocked exception stalled healthy tuple");
    p.d.set_io$$noticeReadyMask(15);p.idle(300);
    p.d.set_io$$exceptionReady(1);p.idle(800);
    for(unsigned n=before;n<p.d.exceptions.size();++n)
        check(u16(p.d.exceptions[n],20)==6000,"existing busy tuple produced a reset");
    check(p.state(1)==4,"exception output modified healthy context");
}

static void reservedTuple(){
    Peer p;p.random=false;p.d.set_io$$exceptionReady(1);
    p.active(0,100000,200000,512);p.autoComplete=false;
    p.write(0,79);p.until([&]{return !p.pending.empty();});p.close(0);
    check(p.state(0)==0&&p.d.get_io$$outstanding()!=0,"closed tuple ownership setup");
    auto before=p.d.exceptions.size();refill(p);
    for(unsigned n=0;n<8;++n){p.wire(p.encode(0,p.peerNext[0],p.planned[0],16));p.idle(70);}
    check(p.d.exceptions.size()==before,"old physical ownership generated CLOSED reset");
    p.autoComplete=true;p.drain();refill(p);
    p.wire(p.encode(0,p.peerNext[0],p.planned[0],16));p.idle(200);
    check(p.d.exceptions.size()==before+1,"fully drained closed tuple did not reset");
    // An in-flight SYN reservation must not become an unknown tuple.
    Peer q;q.random=false;q.d.set_io$$exceptionReady(1);q.open(0,300000,true,512);
    auto syn=q.encode(0,400000,0,2);
    q.wire(syn);q.wire(syn);q.wire(syn);q.idle(500);
    check(q.d.exceptions.empty()&&q.state(0)==3,"pending listener SYN reset");
}
int main(){try{
    unknownAndListener();blockedIsolation();reservedTuple();
    std::cout<<"TCP_EXCEPTION_PASS unknown_listener=pass independent_ledger=pass busy_reserved_tuple=pass "
        <<"stalled_exception_isolation=pass checksum_validation=pass\n";return 0;
}catch(const std::exception&e){std::cerr<<"tcp_exception: "<<e.what()<<'\n';return 1;}}
