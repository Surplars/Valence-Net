#include "ArpHarness.h"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
static void check(bool ok,const char*s){if(!ok)throw std::runtime_error(s);}
constexpr uint32_t LOCAL_IP=0xc0000202;
constexpr uint64_t LOCAL_MAC=0x020000000002ULL,BCAST=0xffffffffffffULL;
static void put(std::vector<uint8_t>&v,unsigned p,unsigned n,uint64_t x){
    for(unsigned i=0;i<n;++i)v[p+i]=x>>(8*(n-i-1));
}
static std::vector<uint8_t> frame(unsigned op,uint64_t smac,uint32_t sip,uint64_t tmac,uint32_t tip){
    std::vector<uint8_t> v(60);put(v,0,6,op==1?BCAST:tmac);put(v,6,6,smac);
    put(v,12,2,0x806);put(v,14,2,1);put(v,16,2,0x800);v[18]=6;v[19]=4;
    put(v,20,2,op);put(v,22,6,smac);put(v,28,4,sip);put(v,32,6,tmac);put(v,38,4,tip);return v;
}
struct Beat{uint64_t lo,hi;unsigned keep;bool last,error;};
struct Result{uint64_t mac;bool error;};
struct Sim{
    SArpHarness d;std::mt19937_64 rng{0x82620261017ULL};std::deque<Beat> rx;
    std::deque<std::vector<uint8_t>> frames;std::deque<Result> results;std::vector<uint8_t> assembling;
    bool ticking=false,txReady=true,resultReady=true,randomStalls=true,query=false,rxDriving=false;
    bool held=false,resultHeld=false;Beat hold{};Result resultHold{};
    uint32_t queryIp=0;unsigned cycles=0,bad=0,conflicts=0,emitted=0,resolved=0;
    void reset(){rx.clear();frames.clear();results.clear();assembling.clear();query=false;rxDriving=false;
        held=false;resultHeld=false;d.set_io$$valid(0);d.set_io$$queryValid(0);d.set_io$$flush(0);
        d.set_io$$tick(0);d.set_io$$outReady(0);d.set_io$$resultReady(0);d.set_reset(1);d.step();d.step();d.set_reset(0);}
    void cycle(bool flush=false){
        if(!rxDriving&&!rx.empty()&&(!randomStalls||rng()%4))rxDriving=true;
        bool tr=txReady&&(!randomStalls||rng()%5<3),rr=resultReady&&(!randomStalls||rng()%5<3);
        d.set_io$$valid(rxDriving);d.set_io$$queryValid(query);d.set_io$$queryIp(queryIp);
        d.set_io$$outReady(tr);d.set_io$$resultReady(rr);d.set_io$$tick(ticking);d.set_io$$flush(flush);
        if(rxDriving){auto b=rx.front();d.set_io$$lo(b.lo);d.set_io$$hi(b.hi);d.set_io$$keep(b.keep);
            d.set_io$$last(b.last);d.set_io$$error(b.error);}
        d.step();++cycles;
        Beat out{d.get_io$$outLo(),d.get_io$$outHi(),unsigned(d.get_io$$outKeep()),bool(d.get_io$$outLast()),false};
        if(held)check(d.get_io$$outValid()&&hold.lo==out.lo&&hold.hi==out.hi&&hold.keep==out.keep&&hold.last==out.last,
                      "ARP frame changed while stalled");
        held=d.get_io$$outValid()&&!tr;if(held)hold=out;
        if(resultHeld&&!flush)check(d.get_io$$resultValid()&&resultHold.mac==d.get_io$$mac()&&
            resultHold.error==bool(d.get_io$$failed()),"ARP result changed while stalled");
        resultHeld=d.get_io$$resultValid()&&!rr;if(resultHeld)resultHold={d.get_io$$mac(),bool(d.get_io$$failed())};
        if(d.get_io$$outValid()&&tr){
            check((out.keep&(out.keep+1))==0,"ARP output keep is sparse");
            unsigned n=__builtin_popcount(out.keep);if(!out.last)check(n==16,"ARP short nonfinal beat");
            for(unsigned i=0;i<n;++i)assembling.push_back((i<8?out.lo:out.hi)>>(8*(i%8)));
            if(out.last){check(assembling.size()==60,"ARP output must include 60-byte padding");
                frames.push_back(assembling);assembling.clear();++emitted;}
        }
        if(d.get_io$$resultValid()&&rr){results.push_back({d.get_io$$mac(),bool(d.get_io$$failed())});++resolved;}
        if(rxDriving&&d.get_io$$ready()){rx.pop_front();rxDriving=false;}
        if(query&&d.get_io$$queryReady())query=false;
        if(d.get_io$$badFrame())++bad;if(d.get_io$$conflict())++conflicts;
    }
    void run(unsigned n){for(unsigned i=0;i<n;++i)cycle();}
    void until(const std::function<bool()>&done,unsigned limit=3000){
        for(unsigned i=0;i<limit&&!done();++i)cycle();check(done(),"ARP simulation timeout");
    }
    void send(const std::vector<uint8_t>&v,bool error=false,bool sparse=false){
        unsigned p=0;do{unsigned n=std::min(16U,unsigned(v.size())-p);Beat b{rng(),rng(),(1U<<n)-1,p+n==v.size(),error&&p+n==v.size()};
            for(unsigned i=0;i<n;++i){auto&w=i<8?b.lo:b.hi;unsigned sh=8*(i%8);w=(w&~(uint64_t(255)<<sh))|(uint64_t(v[p+i])<<sh);}
            if(sparse&&p==0)b.keep^=2;rx.push_back(b);p+=n;
        }while(p<v.size());
    }
    void request(uint32_t ip){check(!query,"overlapping test query");queryIp=ip;query=true;until([&]{return !query;});}
    void expectResult(uint64_t mac,bool error=false){until([&]{return !results.empty();});auto r=results.front();results.pop_front();
        check(r.error==error&&(error||r.mac==mac),"ARP resolution result mismatch");}
    void expectFrame(const std::vector<uint8_t>&v){until([&]{return !frames.empty();});check(frames.front()==v,"ARP wire frame mismatch");frames.pop_front();}
    void quiet(){run(30);check(rx.empty()&&frames.empty()&&results.empty()&&assembling.empty(),"unexpected ARP traffic/result");}
};
int main(){try{
    Sim s;s.reset();
    // Requests teach the cache and generate complete unicast replies.
    for(unsigned i=0;i<120;++i){uint32_t ip=0xc0000210+i;uint64_t mac=0x020102030400ULL+i;
        auto v=frame(1,mac,ip,0,LOCAL_IP);if(i%3==0)v.resize(42);if(i%3==1){v.resize(91);for(unsigned j=42;j<v.size();++j)v[j]=s.rng();}
        s.send(v);s.expectFrame(frame(2,LOCAL_MAC,LOCAL_IP,mac,ip));s.request(ip);s.expectResult(mac);s.quiet();}
    // Special destinations never trigger broadcast ARP probes.
    s.request(LOCAL_IP);s.expectResult(LOCAL_MAC);s.request(0xffffffff);s.expectResult(BCAST);
    s.request(0);s.expectResult(0,true);s.quiet();
    // Cold lookup, resolved by a reply addressed to this endpoint.
    uint32_t peer=0xc00002e0;uint64_t mac=0x021122334455ULL;
    s.request(peer);s.expectFrame(frame(1,LOCAL_MAC,LOCAL_IP,0,peer));
    auto wrongTarget=frame(2,mac,peer,LOCAL_MAC+1,LOCAL_IP);put(wrongTarget,0,6,LOCAL_MAC);
    s.send(wrongTarget);s.until([&]{return s.rx.empty();});s.run(20);
    check(s.results.empty(),"ARP reply addressed to wrong target MAC resolved lookup");
    s.send(frame(2,mac,peer+1,LOCAL_MAC,LOCAL_IP));s.until([&]{return s.rx.empty();});s.run(20);
    check(s.results.empty(),"unrelated ARP reply resolved lookup");
    s.send(frame(2,mac|(1ULL<<40),peer,LOCAL_MAC,LOCAL_IP));s.until([&]{return s.rx.empty();});s.run(20);
    check(s.results.empty(),"multicast sender MAC entered ARP cache");
    s.send(frame(2,mac,peer,LOCAL_MAC,LOCAL_IP));s.expectResult(mac);s.quiet();
    // Existing mappings refresh from gratuitous broadcasts even for other targets.
    uint64_t moved=0x0266778899aaULL;
    s.send(frame(1,moved,peer,0,peer));s.until([&]{return s.rx.empty();});s.run(10);
    s.request(peer);s.expectResult(moved);s.quiet();
    // Held result must not follow subsequent cache changes or expiry.
    s.resultReady=false;s.request(peer);s.run(20);s.ticking=true;s.run(60);s.ticking=false;
    s.resultReady=true;s.expectResult(moved);s.quiet();
    // Expired cache entry triggers a fresh request.
    s.request(peer);s.expectFrame(frame(1,LOCAL_MAC,LOCAL_IP,0,peer));s.send(frame(2,mac,peer,LOCAL_MAC,LOCAL_IP));s.expectResult(mac);s.quiet();
    // Invalid Ethernet/ARP fields and every truncation length never learn or reply.
    unsigned beforeBad=s.bad;
    for(unsigned n=0;n<42;++n){auto v=frame(1,mac,peer,0,LOCAL_IP);v.resize(n);s.send(v);s.until([&]{return s.rx.empty();});s.quiet();}
    for(unsigned mode=0;mode<10;++mode){auto v=frame(1,mac,peer,0,LOCAL_IP);
        if(mode==0)v[12]^=1;if(mode==1)v[15]=2;if(mode==2)v[17]=1;if(mode==3)v[18]=5;
        if(mode==4)v[19]=16;if(mode==5)v[21]=3;if(mode==6)put(v,0,6,LOCAL_MAC+1);if(mode==7)v[6]^=2;
        s.send(v,mode==8,mode==9);s.until([&]{return s.rx.empty();});s.quiet();}
    check(s.bad-beforeBad==52,"ARP malformed-frame accounting");
    // IPv4 address probes (sender IP zero) get replies but never become cache entries.
    s.send(frame(1,mac,0,0,LOCAL_IP));s.expectFrame(frame(2,LOCAL_MAC,LOCAL_IP,mac,0));s.quiet();
    // Source-address conflict is surfaced rather than learned as a neighbor.
    unsigned beforeConflict=s.conflicts;s.send(frame(1,mac,LOCAL_IP,0,LOCAL_IP));
    s.expectFrame(frame(2,LOCAL_MAC,LOCAL_IP,mac,LOCAL_IP));check(s.conflicts==beforeConflict+1,"ARP conflict not reported");s.quiet();
    // No retry timer can run while the first request frame is blocked at TX.
    uint32_t absent=0xc00002fe;s.txReady=false;s.ticking=true;s.request(absent);s.run(100);
    check(s.results.empty()&&s.frames.empty(),"ARP timed out before request actually transmitted");
    s.txReady=true;
    for(unsigned attempt=0;attempt<3;++attempt)s.expectFrame(frame(1,LOCAL_MAC,LOCAL_IP,0,absent));
    s.expectResult(0,true);s.ticking=false;s.quiet();
    // Flush cancels a pending lookup while an already accepted frame finishes.
    s.txReady=false;s.request(absent);s.run(20);s.cycle(true);s.expectResult(0,true);
    s.txReady=true;s.expectFrame(frame(1,LOCAL_MAC,LOCAL_IP,0,absent));s.ticking=true;s.run(60);s.ticking=false;s.quiet();
    // Fill all four slots and prove round-robin eviction triggers resolution.
    s.cycle(true);s.run(2);
    for(unsigned i=0;i<5;++i){s.send(frame(1,mac+i,0xc0000230+i,0,LOCAL_IP));
        s.expectFrame(frame(2,LOCAL_MAC,LOCAL_IP,mac+i,0xc0000230+i));}
    s.request(0xc0000230);s.expectFrame(frame(1,LOCAL_MAC,LOCAL_IP,0,0xc0000230));
    s.send(frame(2,mac,0xc0000230,LOCAL_MAC,LOCAL_IP));s.expectResult(mac);s.quiet();
    // Reset in the middle of an ingress frame cannot leave stale parse state.
    auto partial=frame(1,mac,peer,0,LOCAL_IP);s.send(partial);
    s.until([&]{return s.rx.size()<=2;});s.reset();s.send(frame(1,mac,peer,0,LOCAL_IP));
    s.expectFrame(frame(2,LOCAL_MAC,LOCAL_IP,mac,peer));s.request(peer);s.expectResult(mac);s.quiet();
    std::cout<<"ARP_ENDPOINT_PASS learned_peers=120 malformed=52 retries=3 expiry=pass flush=pass eviction=pass wrong_replies=pass"
             <<" tx_frames="<<s.emitted<<" resolutions="<<s.resolved<<" cycles="<<s.cycles<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
