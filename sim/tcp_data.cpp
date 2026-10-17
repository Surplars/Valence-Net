#include "TcpDataHarness.h"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>
#include <tuple>
#include <vector>
static void check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
static void put16(std::vector<uint8_t>&v,unsigned p,uint16_t n){v[p]=n>>8;v[p+1]=n;}
static void put32(std::vector<uint8_t>&v,unsigned p,uint32_t n){put16(v,p,n>>16);put16(v,p+2,n);}
static uint16_t checksum(std::vector<uint8_t>v){
    if(v.size()%2)v.push_back(0);uint32_t s=0;
    for(unsigned i=0;i<v.size();i+=2)s+=(unsigned(v[i])<<8)|v[i+1];
    while(s>>16)s=(s&65535)+(s>>16);return uint16_t(~s);
}
struct Block{std::vector<uint8_t> bytes;uint64_t context;unsigned cursor=0;std::optional<uint32_t> base;};
struct Writer{Block block;unsigned mode,length,pos=0,n=0;bool allocated=false,driving=false,complete=false;uint64_t lo=0,hi=0;unsigned keep=0;};
struct Segment{
    uint32_t generation,sequence;unsigned length;uint64_t context;bool retry,push;
    auto operator<=>(const Segment&)const=default;
};
struct Packet{Segment desc;std::vector<uint8_t> bytes;unsigned pos=0;};
struct Test{
    STcpDataHarness d;std::mt19937_64 rng{0x10feed9293ULL};
    std::vector<Block> blocks;std::deque<unsigned> unscheduled;
    std::optional<Writer> writer;std::optional<uint32_t> replay;
    std::deque<Segment> scheduled;std::deque<Packet> wire;
    uint32_t next=0,oldest=0,actual=0,generation=0;unsigned mss=1460,credit=65535,ident=0;
    bool enabled=true,stallOutput=false,stallStored=false,stallReserve=false,random=true,autoAck=false;
    uint64_t nextContext=1;
    using Beat=std::tuple<uint64_t,uint64_t,unsigned,bool,bool,Segment>;
    std::optional<Beat> held;
    using Result=std::tuple<uint32_t,unsigned,uint64_t,bool>;
    std::optional<Result> heldStored;
    unsigned cycles=0,packets=0,prepared=0,reservations=0,writes=0,badWrites=0,replays=0,misses=0,resets=0;
    unsigned concurrent=0,overlap=0,ackDuringRetry=0,firstEnd=0,lastEnd=0;
    bool retryOutstanding=false;
    Test(){reset(0xfffffff0);}
    void reset(uint32_t seq){
        d.set_io$$allocateValid(0);d.set_io$$valid(0);d.set_io$$replayValid(0);
        d.set_io$$outReady(0);d.set_io$$storedReady(0);d.set_io$$reserveReady(0);d.set_io$$enabled(0);
        d.set_reset(1);d.step();d.step();d.set_reset(0);
        blocks.clear();unscheduled.clear();scheduled.clear();wire.clear();writer.reset();replay.reset();held.reset();heldStored.reset();
        next=oldest=actual=seq;++generation;ident=0;enabled=true;random=true;
        stallOutput=stallStored=stallReserve=autoAck=false;mss=1460;credit=65535;retryOutstanding=false;++resets;
        for(unsigned i=0;i<5;++i)step();
        check(d.get_io$$freeBanks()==4,"reset did not free TX banks");
    }
    void write(unsigned size,unsigned mode=0){
        check(!writer,"test overlapping writes");Writer w{};w.mode=mode;w.length=size;w.block.context=nextContext++;
        w.block.bytes.resize(size);for(auto&b:w.block.bytes)b=rng();
        if(mode==1)++w.length;if(mode==2)--w.length;
        writer=std::move(w);
    }
    Segment descriptor(bool prep){
        if(prep)return {uint32_t(d.get_io$$preparedGeneration()),uint32_t(d.get_io$$preparedSequence()),
            unsigned(d.get_io$$preparedLength()),d.get_io$$preparedContext(),
            bool(d.get_io$$preparedRetransmission()),bool(d.get_io$$preparedPush())};
        return {uint32_t(d.get_io$$packetGeneration()),uint32_t(d.get_io$$packetSequence()),
            unsigned(d.get_io$$packetLength()),d.get_io$$packetContext(),
            bool(d.get_io$$packetRetransmission()),bool(d.get_io$$packetPush())};
    }
    std::vector<uint8_t> encode(const Segment&s){
        auto it=std::find_if(blocks.begin(),blocks.end(),[&](const Block&b){return b.context==s.context;});
        check(it!=blocks.end()&&it->base.has_value(),"packet lost application context");
        unsigned off=s.sequence-*it->base;check(off+s.length<=it->bytes.size(),"packet outside original bytes");
        std::vector<uint8_t> tcp(20);
        put16(tcp,0,12345);put16(tcp,2,443);put32(tcp,4,s.sequence);put32(tcp,8,0x98765432);
        tcp[12]=0x50;tcp[13]=s.push?24:16;put16(tcp,14,64000);
        tcp.insert(tcp.end(),it->bytes.begin()+off,it->bytes.begin()+off+s.length);
        std::vector<uint8_t> pseudo(12);put32(pseudo,0,0x0a000001);put32(pseudo,4,0x0a000002);
        pseudo[9]=6;put16(pseudo,10,tcp.size());pseudo.insert(pseudo.end(),tcp.begin(),tcp.end());
        put16(tcp,16,checksum(pseudo));
        std::vector<uint8_t> ip(20);ip[0]=0x45;ip[1]=0x28;put16(ip,2,20+tcp.size());
        put16(ip,4,ident++);put16(ip,6,0x4000);ip[8]=64;ip[9]=6;
        put32(ip,12,0x0a000001);put32(ip,16,0x0a000002);put16(ip,10,checksum(ip));
        ip.insert(ip.end(),tcp.begin(),tcp.end());return ip;
    }
    std::optional<Segment> replayExpected(uint32_t seq){
        if(uint32_t(seq-oldest)>=uint32_t(actual-oldest)||!mss)return {};
        for(const auto&b:blocks)if(b.base&&uint32_t(seq-*b.base)<b.cursor){
            unsigned off=seq-*b.base,n=std::min({mss,b.cursor-off,unsigned(actual-seq)});
            return Segment{generation,seq,n,b.context,true,off+n==b.bytes.size()};
        }
        return {};
    }
    void ack(uint32_t seq){
        check(uint32_t(seq-oldest)<=uint32_t(actual-oldest),"test ACK beyond physical transmission");
        if(retryOutstanding&&seq!=oldest)++ackDuringRetry;
        oldest=seq;
    }
    void step(){
        bool av=writer&&!writer->allocated;
        d.set_io$$allocateValid(av);
        if(writer){auto&w=*writer;d.set_io$$length(w.length);d.set_io$$context(w.block.context);
            if(w.allocated&&!w.complete&&!w.driving&&(!random||rng()%5)){
                w.driving=true;w.n=std::min(16U,unsigned(w.block.bytes.size())-w.pos);w.lo=rng();w.hi=rng();
                for(unsigned i=0;i<w.n;++i){auto&word=i<8?w.lo:w.hi;unsigned shift=8*(i%8);
                    word=(word&~(uint64_t(255)<<shift))|(uint64_t(w.block.bytes[w.pos+i])<<shift);}
                w.keep=(1U<<w.n)-1;if(w.mode==3&&w.pos==0)w.keep^=2;
            }
            if(w.driving){d.set_io$$lo(w.lo);d.set_io$$hi(w.hi);d.set_io$$keep(w.keep);
                d.set_io$$last(w.pos+w.n==w.block.bytes.size());d.set_io$$error(w.mode==4&&w.pos+w.n==w.block.bytes.size());}
        }
        bool iv=writer&&writer->driving,sr=!stallStored&&(!random||rng()%4),
             rr=!stallReserve&&(!random||rng()%3),outReady=!stallOutput&&(!random||rng()%5);
        d.set_io$$valid(iv);d.set_io$$storedReady(sr);d.set_io$$reserveReady(rr);d.set_io$$outReady(outReady);
        d.set_io$$enabled(enabled);d.set_io$$generation(generation);d.set_io$$sendOldest(oldest);
        d.set_io$$sendActual(actual);d.set_io$$sendSequence(next);d.set_io$$credit(credit);d.set_io$$mss(mss);
        d.set_io$$replayValid(bool(replay));d.set_io$$replaySequence(replay.value_or(0));
        d.set_io$$sourceIp(0x0a000001);d.set_io$$destinationIp(0x0a000002);
        d.set_io$$sourcePort(12345);d.set_io$$destinationPort(443);d.set_io$$acknowledgement(0x98765432);
        d.set_io$$window(64000);d.set_io$$ttl(64);d.set_io$$tos(0x28);
        d.step();++cycles;check(!d.get_io$$fault(),"internal TX ownership or serialization fault");
        if(held){Beat now={d.get_io$$outLo(),d.get_io$$outHi(),unsigned(d.get_io$$outKeep()),
            bool(d.get_io$$outLast()),bool(d.get_io$$outError()),descriptor(false)};
            check(d.get_io$$outValid()&&now==*held,"wire output changed under backpressure");}
        held=d.get_io$$outValid()&&!outReady?std::optional<Beat>({d.get_io$$outLo(),d.get_io$$outHi(),
            unsigned(d.get_io$$outKeep()),bool(d.get_io$$outLast()),bool(d.get_io$$outError()),descriptor(false)}):std::nullopt;
        Result result={uint32_t(d.get_io$$storedToken()),unsigned(d.get_io$$storedLength()),
            d.get_io$$storedContext(),bool(d.get_io$$storedError())};
        if(heldStored)check(d.get_io$$storedValid()&&result==*heldStored,"store result changed while stalled");
        heldStored=d.get_io$$storedValid()&&!sr?std::optional<Result>(result):std::nullopt;

        // Validate reservations/replay reads independently of emitted metadata.
        if(d.get_io$$reserveValid()&&rr){
            check(enabled&&!unscheduled.empty()&&!replay,"unexpected sequence reservation");
            auto&b=blocks[unscheduled.front()];
            unsigned n=std::min({unsigned(b.bytes.size())-b.cursor,mss,credit});
            check(n&&d.get_io$$reserveLength()==n,"MSS or credit segmentation mismatch");
            if(!b.base)b.base=next;
            check(next==uint32_t(*b.base+b.cursor),"noncontiguous block reservation");
            b.cursor+=n;scheduled.push_back({generation,next,n,b.context,false,b.cursor==b.bytes.size()});
            next+=n;++reservations;
            if(b.cursor==b.bytes.size())unscheduled.pop_front();
        }
        bool replayFire=replay&&d.get_io$$replayReady();
        auto retry=replayFire?replayExpected(*replay):std::nullopt;
        check(bool(d.get_io$$replayMiss())==(replayFire&&!retry),"replay hit/miss mismatch");
        if(replayFire){if(retry){scheduled.push_back(*retry);++replays;retryOutstanding=true;}else ++misses;replay.reset();}
        if(d.get_io$$preparedValid()){
            check(!scheduled.empty(),"serializer command without retained descriptor");
            auto desc=descriptor(true);
            if(desc!=scheduled.front()){
                std::cerr<<"prepared seq="<<desc.sequence<<" len="<<desc.length<<" retry="<<desc.retry
                    <<" expected seq="<<scheduled.front().sequence<<" len="<<scheduled.front().length<<'\n';
                throw std::runtime_error("prepared descriptor mismatch");
            }
            if(!wire.empty())++overlap;
            scheduled.pop_front();wire.push_back({desc,encode(desc),0});++prepared;
        }
        if(d.get_io$$outValid()&&outReady){
            check(!wire.empty(),"unexpected wire packet");auto&p=wire.front();
            check(descriptor(false)==p.desc&&!d.get_io$$outError(),"packet context/descriptor corruption");
            unsigned keep=d.get_io$$outKeep(),n=__builtin_popcount(keep);
            check((keep&(keep+1))==0&&p.pos+n<=p.bytes.size(),"invalid wire keep/length");
            for(unsigned i=0;i<n;++i){
                uint64_t w=i<8?d.get_io$$outLo():d.get_io$$outHi();
                if(uint8_t(w>>(8*(i%8)))!=p.bytes[p.pos+i]){
                    std::cerr<<"packet "<<packets<<" byte "<<p.pos+i<<'\n';
                    throw std::runtime_error("IPv4/TCP wire bytes mismatch");
                }
            }
            p.pos+=n;
            if(d.get_io$$outLast()){
                check(p.pos==p.bytes.size(),"short wire packet");
                if(!p.desc.retry){check(actual==p.desc.sequence,"new data completed out of order");actual+=p.desc.length;}
                else retryOutstanding=false;
                wire.pop_front();++packets;lastEnd=cycles;if(!firstEnd)firstEnd=cycles;
                if(autoAck)ack(actual);
            }else check(n==16,"nonfinal short wire beat");
            if(iv&&d.get_io$$ready())++concurrent;
        }
        if(d.get_io$$storedValid()&&sr){
            check(writer&&writer->complete,"write reported before last beat");
            auto&w=*writer;bool bad=w.mode||w.block.bytes.size()>2048;
            check(std::get<1>(result)==w.length&&std::get<2>(result)==w.block.context&&std::get<3>(result)==bad,
                "application write validation mismatch");
            if(!bad&&!w.block.bytes.empty()){unscheduled.push_back(blocks.size());blocks.push_back(w.block);}
            ++writes;if(bad)++badWrites;writer.reset();
        }
        if(av&&d.get_io$$allocateReady()){check(bool(writer),"allocation without writer");writer->allocated=true;}
        if(iv&&d.get_io$$ready()){check(bool(writer),"input without writer");auto&w=*writer;
            w.pos+=w.n;w.driving=false;if(w.pos==w.block.bytes.size())w.complete=true;}
    }
    template<class F>void until(F done,unsigned max=20000){unsigned n=0;while(!done()){step();if(++n>=max){std::cerr<<"timeout generation="<<generation<<" cycle="<<cycles<<" next="<<next<<" actual="<<actual<<" oldest="<<oldest<<" pending="<<scheduled.size()<<" wire="<<wire.size()<<" replay="<<bool(replay)<<" free="<<d.get_io$$freeBanks()<<std::endl;throw std::runtime_error("TX progress timeout");}}}
    void drain(){until([&]{return !writer&&unscheduled.empty()&&scheduled.empty()&&wire.empty()&&!replay;});for(unsigned i=0;i<8;++i)step();}
    void writeWait(unsigned n,unsigned mode=0){write(n,mode);until([&]{return !writer;});}
    void retry(uint32_t seq){check(!replay,"test replay collision");replay=seq;until([&]{return !replay;});}
    void reclaim(){ack(actual);for(unsigned i=0;i<12;++i)step();check(d.get_io$$freeBanks()==4,"ACK failed to reclaim all banks");}
};
int main(){try{
    Test t;
    // A stalled write result must not reserve sequence space.
    t.stallStored=true;t.write(83);t.until([&]{return t.d.get_io$$storedValid();});
    for(unsigned i=0;i<20;++i)t.step();check(t.reservations==0,"unowned store result was transmitted");
    t.stallStored=false;t.drain();t.reclaim();
    // MSS segmentation, odd offsets, partial ACK, and wrap around zero.
    t.reset(0xffffffb0);t.mss=63;t.writeWait(255);t.drain();
    check(t.next==uint32_t(0xffffffb0U+255),"sequence wrap");
    t.ack(uint32_t(0xffffffb0U+17));t.retry(t.oldest);t.drain();
    t.ack(uint32_t(0xffffffb0U+66));t.mss=41;t.retry(t.oldest);t.drain();
    t.retry(t.oldest-1);t.retry(t.actual);t.retry(t.actual+0x80000000U);t.reclaim();
    // A speculative lookup is not an accepted request. Withdraw/change it,
    // or ACK the entire bank while lookup owns its pin; no stale data escapes.
    t.reset(500);t.writeWait(200);t.drain();
    auto retriesBefore=t.replays;
    t.replay=t.oldest;t.step();t.replay.reset();t.drain();
    check(t.replays==retriesBefore,"withdrawn speculative replay was emitted");
    t.replay=t.oldest;t.step();t.replay=t.actual;
    t.until([&]{return !t.replay;});t.drain();
    check(t.replays==retriesBefore,"changed speculative replay used stale lookup");
    t.replay=t.oldest;t.step();t.ack(t.actual);
    t.until([&]{return !t.replay;});t.drain();t.reclaim();
    check(t.replays==retriesBefore,"ACK during lookup did not cancel replay");
    // A window smaller than MSS sends the permitted byte count; zero credit
    // suppresses new data but must not block retransmission of outstanding data.
    t.reset(1000);t.credit=0;t.writeWait(200);
    auto before=t.reservations;for(unsigned i=0;i<30;++i)t.step();
    check(t.reservations==before,"zero-credit reservation");
    t.credit=7;t.until([&]{return t.reservations>before;});t.credit=0;
    t.until([&]{return t.actual==1007;});t.retry(1000);t.until([&]{return t.wire.empty()&&t.scheduled.empty();});
    t.credit=65535;t.drain();t.reclaim();
    // Physical output can stall while more than one packet is prepared.
    t.reset(9000);t.stallOutput=true;t.mss=200;t.writeWait(1000);
    auto startPrepared=t.prepared;
    t.until([&]{return t.next>9000;});
    t.retry(9000); // A reserved segment has not yet reached physical output.
    for(unsigned i=0;i<300;++i)t.step();
    check(t.actual==9000&&t.next>9000&&t.prepared>=startPrepared+2,"TX serialized behind physical completion");
    t.stallOutput=false;t.drain();t.reclaim();
    // Reclaim a bank while a replay is queued/stalled, then overwrite/reuse
    // every bank. The already-pinned read and serialized bytes must survive.
    for(unsigned offset: {0U,1U,15U,16U,17U,127U,511U,1460U,2047U}){
        t.reset(0xfffff800);t.writeWait(2048);t.drain();t.ack(0xfffff800U+offset);
        t.stallOutput=true;t.retry(t.oldest);t.ack(t.actual);
        t.writeWait(123);t.writeWait(257);t.writeWait(61);
        for(unsigned i=0;i<70;++i)t.step();
        t.stallOutput=false;t.drain();t.reclaim();
    }
    // Exhaust all banks with unacknowledged data, then release exactly one.
    t.reset(123);for(unsigned n:{31U,500U,2048U,1U}){t.writeWait(n);t.drain();}
    check(t.d.get_io$$freeBanks()==0,"unacknowledged data was reclaimed");
    t.write(99);for(unsigned i=0;i<40;++i)t.step();
    check(!t.writer->allocated,"full store accepted another write");
    t.ack(123+31);t.drain();t.reclaim();
    // Empty and malformed application writes do not consume TCP sequence space.
    t.reset(0x12345678);for(unsigned i=0;i<240;++i){
        unsigned mode=i%6,size=mode==5?2200:1+t.rng()%2048;
        auto seq=t.next;t.writeWait(mode==0?0:size,mode==5?0:mode);t.drain();
        check(t.next==seq,"bad/empty write consumed sequence space");t.reclaim();
    }
    // Concurrent writes, arbitrary credit/MSS changes, partial/cumulative ACKs,
    // repeated timeout requests, and allocation pressure over multiple epochs.
    for(unsigned epoch=0;epoch<20;++epoch){
        t.reset(uint32_t(t.rng()));unsigned launched=0;
        for(unsigned n=0;n<18000;++n){
            if(!t.writer&&launched<60&&t.rng()%3==0){t.write(1+t.rng()%2048);++launched;}
            t.mss=1+t.rng()%1600;t.credit=t.rng()%7?1+t.rng()%3000:0;
            if(uint32_t(t.actual-t.oldest)&&t.rng()%13==0)t.ack(t.oldest+1+t.rng()%uint32_t(t.actual-t.oldest));
            if(!t.replay&&t.rng()%101==0)t.replay=t.rng()%4?t.oldest:t.actual;
            t.step();
            if(launched==60&&!t.writer&&t.unscheduled.empty()&&t.wire.empty()&&t.scheduled.empty()&&!t.replay)break;
            check(n!=17999,"random traffic did not drain");
        }
        t.credit=65535;t.drain();t.reclaim();
    }
    // Reset during a write, a pinned replay, and stalled serialized output.
    t.reset(0);t.write(2048);t.until([&]{return t.writer->pos>=32;});t.reset(77);
    t.writeWait(2048);t.drain();t.stallOutput=true;t.retry(77);t.reset(88);
    t.writeWait(64);t.drain();t.reclaim();
    for(unsigned payload:{1U,64U,1460U}){
        t.reset(0x80000000);t.random=false;t.autoAck=true;t.mss=1460;
        unsigned completed=t.packets;t.firstEnd=t.lastEnd=0;
        for(unsigned i=0;i<256;++i)t.writeWait(payload);
        t.drain();check(t.packets-completed==256,"throughput packet count");
        double period=double(t.lastEnd-t.firstEnd)/255;
        unsigned ethernet=std::max(60U,14U+40U+payload)+4+8+12;
        std::cout<<"TCP_DATA_SERVICE payload="<<payload<<" cycles_per_packet="<<period
            <<" hypothetical_gbps_at_156_25MHz="<<ethernet*8*.15625/period<<'\n';
        t.reclaim();
    }
    check(t.overlap&&t.concurrent&&t.ackDuringRetry,"missing pipeline/replay overlap coverage");
    std::cout<<"TCP_DATA_PASS cycles="<<t.cycles<<" writes="<<t.writes<<" rejected="<<t.badWrites
        <<" reservations="<<t.reservations<<" packets="<<t.packets<<" replays="<<t.replays<<" misses="<<t.misses
        <<" prepare_overlap="<<t.overlap<<" simultaneous_io="<<t.concurrent<<" ack_during_retry="<<t.ackDuringRetry
        <<" resets="<<t.resets<<" wire_bytes=pass partial_ack=pass wrap=pass backpressure=pass\n";
    return 0;
}catch(const std::exception&e){std::cerr<<"TCP_DATA_FAIL: "<<e.what()<<'\n';return 1;}}
