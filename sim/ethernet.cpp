#include "EthernetHarness.h"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <iomanip>
#include <map>
#include <random>
#include <stdexcept>
#include <vector>
static void check(bool ok,const char*s){if(!ok)throw std::runtime_error(s);}
constexpr uint32_t LOCAL_IP=0xc0000202,GATEWAY=0xc0000201,PEER=0xc0000210;
constexpr uint64_t LOCAL_MAC=0x020000000002ULL,PEER_MAC=0x021122334455ULL,GW_MAC=0x0266778899aaULL,BCAST=0xffffffffffffULL;
static void put(std::vector<uint8_t>&v,unsigned p,unsigned n,uint64_t x){for(unsigned i=0;i<n;++i)v[p+i]=x>>(8*(n-i-1));}
static uint64_t get(const std::vector<uint8_t>&v,unsigned p,unsigned n){uint64_t x=0;for(unsigned i=0;i<n;++i)x=(x<<8)|v.at(p+i);return x;}
static uint16_t checksum(std::vector<uint8_t>v){if(v.size()%2)v.push_back(0);uint32_t s=0;
    for(unsigned i=0;i<v.size();i+=2)s+=(v[i]<<8)|v[i+1];while(s>>16)s=(s&65535)+(s>>16);return uint16_t(~s);}
static std::vector<uint8_t> arp(unsigned op,uint64_t sm,uint32_t si,uint64_t tm,uint32_t ti){
    std::vector<uint8_t>v(60);put(v,0,6,op==1?BCAST:tm);put(v,6,6,sm);put(v,12,2,0x806);
    put(v,14,2,1);put(v,16,2,0x800);v[18]=6;v[19]=4;put(v,20,2,op);
    put(v,22,6,sm);put(v,28,4,si);put(v,32,6,tm);put(v,38,4,ti);return v;
}
static std::vector<uint8_t> udp(uint64_t sm,uint64_t dm,uint32_t si,uint32_t di,uint16_t sp,uint16_t dp,
                               uint16_t id,const std::vector<uint8_t>&payload){
    std::vector<uint8_t>v(42);put(v,0,6,dm);put(v,6,6,sm);put(v,12,2,0x800);v[14]=0x45;
    put(v,16,2,28+payload.size());put(v,18,2,id);put(v,20,2,0x4000);v[22]=64;v[23]=17;
    put(v,26,4,si);put(v,30,4,di);put(v,34,2,sp);put(v,36,2,dp);put(v,38,2,8+payload.size());
    std::vector<uint8_t>h(v.begin()+14,v.begin()+34);put(v,24,2,checksum(h));
    v.insert(v.end(),payload.begin(),payload.end());std::vector<uint8_t>p(12);
    put(p,0,4,si);put(p,4,4,di);p[9]=17;put(p,10,2,8+payload.size());p.insert(p.end(),v.begin()+34,v.end());
    uint16_t c=checksum(p);put(v,40,2,c?c:65535);v.resize(std::max<size_t>(60,v.size()),0);return v;
}
struct Beat{uint64_t lo,hi;unsigned keep;bool last,error;};
struct Command{uint32_t dst;uint16_t sp,dp,id;std::vector<uint8_t>data;bool bad;};
struct Received{uint32_t src,dst;uint16_t sp,dp;std::vector<uint8_t>data;};
struct Sim{
    SEthernetHarness d;std::mt19937_64 rng{0xe791768826ULL};std::deque<Beat>rx;
    std::deque<Command>commands;std::deque<std::vector<uint8_t>>expectedTx,expectedArpReplies;
    std::deque<Received>expectedRx;std::vector<uint8_t>wire;
    std::vector<unsigned>txTimes,rxTimes;
    std::map<uint32_t,uint64_t>neighbors{{PEER,PEER_MAC},{GATEWAY,GW_MAC}};
    bool tick=false,txReady=true,outReady=true,random=true,rxDrive=false,cmdDrive=false,cmdSent=false,dataDrive=false;
    bool txHeld=false,rxHeld=false;Beat txHold{},rxHold{};
    unsigned pos=0,count=0,receivedPos=0,cycles=0,txDrops=0,rxDrops=0,txPackets=0,rxPackets=0,probes=0,replies=0;
    uint32_t gateway=GATEWAY;unsigned mtu=900;
    void inject(const std::vector<uint8_t>&v,bool error=false,bool sparse=false){unsigned p=0;
        do{unsigned n=std::min(16U,unsigned(v.size())-p);Beat b{rng(),rng(),(1U<<n)-1,p+n==v.size(),error&&p+n==v.size()};
            for(unsigned i=0;i<n;++i){auto&w=i<8?b.lo:b.hi;unsigned sh=8*(i%8);w=(w&~(uint64_t(255)<<sh))|(uint64_t(v[p+i])<<sh);}
            if(sparse&&p==0)b.keep^=0x8000;rx.push_back(b);p+=n;
        }while(p<v.size());}
    void queue(uint32_t dst,uint64_t mac,unsigned size,unsigned id,bool drop=false,bool bad=false){
        Command c{dst,uint16_t(12000+id),uint16_t(22000+id),uint16_t(id),std::vector<uint8_t>(size),bad};
        for(auto&b:c.data)b=rng();if(!drop)expectedTx.push_back(udp(LOCAL_MAC,mac,LOCAL_IP,dst,c.sp,c.dp,c.id,c.data));commands.push_back(c);
    }
    void injectRequest(uint64_t mac,uint32_t ip){inject(arp(1,mac,ip,0,LOCAL_IP));expectedArpReplies.push_back(arp(2,LOCAL_MAC,LOCAL_IP,mac,ip));}
    void onFrame(){
        check(wire.size()>=60,"short Ethernet TX frame");unsigned type=get(wire,12,2);
        if(type==0x806){unsigned op=get(wire,20,2);uint32_t target=get(wire,38,4);
            if(op==1){check(wire==arp(1,LOCAL_MAC,LOCAL_IP,0,target),"bad ARP probe on shared port");++probes;
                if(neighbors.count(target))inject(arp(2,neighbors.at(target),target,LOCAL_MAC,LOCAL_IP));
            }else{check(!expectedArpReplies.empty()&&wire==expectedArpReplies.front(),"bad/interleaved ARP reply");expectedArpReplies.pop_front();++replies;}
        }else{if(type!=0x800||expectedTx.empty()){std::cerr<<"unexpected type="<<std::hex<<type<<std::dec<<" bytes="<<wire.size()
                <<" tx="<<txPackets<<" probes="<<probes<<" expected="<<expectedTx.size()<<" cycles="<<cycles<<'\n';
                for(unsigned i=0;i<std::min<size_t>(wire.size(),32);++i)std::cerr<<std::hex<<unsigned(wire[i])<<' ';std::cerr<<std::dec<<'\n';}
            check(type==0x800&&!expectedTx.empty(),"unexpected Ethernet data packet");
            if(wire!=expectedTx.front()){std::cerr<<"actual="<<wire.size()<<" expected="<<expectedTx.front().size()<<" id="<<get(wire,18,2)<<'\n';
                for(unsigned i=0;i<std::min(wire.size(),expectedTx.front().size());++i)if(wire[i]!=expectedTx.front()[i]){std::cerr<<"first mismatch="<<i<<'\n';break;}
                throw std::runtime_error("Ethernet IPv4 bytes/route/padding mismatch");}
            expectedTx.pop_front();++txPackets;txTimes.push_back(cycles);
        }wire.clear();
    }
    static bool same(const Beat&a,const Beat&b){return a.lo==b.lo&&a.hi==b.hi&&a.keep==b.keep&&a.last==b.last&&a.error==b.error;}
    void cycle(bool flush=false){
        if(!rxDrive&&!rx.empty()&&(!random||rng()%4))rxDrive=true;
        d.set_io$$rxValid(rxDrive);
        if(rxDrive){auto b=rx.front();d.set_io$$rxLo(b.lo);d.set_io$$rxHi(b.hi);d.set_io$$rxKeep(b.keep);d.set_io$$rxLast(b.last);d.set_io$$rxError(b.error);}
        if(!commands.empty()){
            auto&c=commands.front();
            if(!cmdSent&&!cmdDrive&&(!random||rng()%4)){
                cmdDrive=true;d.set_io$$destinationIp(c.dst);d.set_io$$sourcePort(c.sp);d.set_io$$destinationPort(c.dp);
                d.set_io$$length(c.data.size());d.set_io$$identification(c.id);
            }
            if(cmdSent&&!dataDrive&&(!random||rng()%5)){
                dataDrive=true;count=std::min(16U,unsigned(c.data.size())-pos);uint64_t lo=rng(),hi=rng();
                for(unsigned i=0;i<count;++i){auto&w=i<8?lo:hi;unsigned sh=8*(i%8);w=(w&~(uint64_t(255)<<sh))|(uint64_t(c.data[pos+i])<<sh);}
                d.set_io$$inLo(lo);d.set_io$$inHi(hi);d.set_io$$inKeep((1U<<count)-1);
                d.set_io$$inLast(pos+count==c.data.size());d.set_io$$inError(c.bad&&pos+count==c.data.size());
            }
        }
        d.set_io$$commandValid(cmdDrive);d.set_io$$inValid(dataDrive);
        bool tr=txReady&&(!random||rng()%5<3),rr=outReady&&(!random||rng()%5<3);
        d.set_io$$txReady(tr);d.set_io$$outReady(rr);d.set_io$$tick(tick);d.set_io$$flush(flush);
        d.set_io$$gateway(gateway);d.set_io$$mtu(mtu);d.step();++cycles;
        Beat t{d.get_io$$txLo(),d.get_io$$txHi(),unsigned(d.get_io$$txKeep()),bool(d.get_io$$txLast()),bool(d.get_io$$txError())};
        Beat r{d.get_io$$outLo(),d.get_io$$outHi(),unsigned(d.get_io$$outKeep()),bool(d.get_io$$outLast()),bool(d.get_io$$outError())};
        if(txHeld)check(d.get_io$$txValid()&&same(t,txHold),"shared Ethernet TX changed under stall");
        if(rxHeld)check(d.get_io$$outValid()&&same(r,rxHold),"Ethernet UDP payload changed under stall");
        txHeld=d.get_io$$txValid()&&!tr;rxHeld=d.get_io$$outValid()&&!rr;if(txHeld)txHold=t;if(rxHeld)rxHold=r;
        if(d.get_io$$txValid()&&tr){check(!t.error&&(t.keep&(t.keep+1))==0,"bad TX beat");
            unsigned n=__builtin_popcount(t.keep);if(!t.last)check(n==16,"short nonfinal Ethernet TX beat");
            for(unsigned i=0;i<n;++i)wire.push_back((i<8?t.lo:t.hi)>>(8*(i%8)));if(t.last)onFrame();}
        if(d.get_io$$outValid()){
            check(!expectedRx.empty()&&!r.error,"unexpected/error received UDP payload");auto&e=expectedRx.front();
            check(d.get_io$$outSourceIp()==e.src&&d.get_io$$outDestinationIp()==e.dst&&d.get_io$$outSourcePort()==e.sp&&
                d.get_io$$outDestinationPort()==e.dp&&d.get_io$$outLength()==e.data.size(),"Ethernet RX metadata mismatch");
            if(rr){unsigned n=__builtin_popcount(r.keep);check((r.keep&(r.keep+1))==0&&receivedPos+n<=e.data.size(),"bad RX payload length");
                for(unsigned i=0;i<n;++i)check(uint8_t((i<8?r.lo:r.hi)>>(8*(i%8)))==e.data[receivedPos+i],"Ethernet RX payload byte mismatch");
                receivedPos+=n;if(r.last){check(receivedPos==e.data.size(),"truncated Ethernet RX payload");expectedRx.pop_front();receivedPos=0;++rxPackets;rxTimes.push_back(cycles);}
                else check(n==16,"short nonfinal RX payload beat");}
        }
        if(rxDrive&&d.get_io$$rxReady()){rx.pop_front();rxDrive=false;}
        if(cmdDrive&&d.get_io$$commandReady()){cmdDrive=false;cmdSent=true;}
        if(dataDrive&&d.get_io$$inReady()){
            pos+=count;dataDrive=false;if(pos==commands.front().data.size()){commands.pop_front();pos=0;cmdSent=false;}
        }
        rxDrops+=d.get_io$$rxDrops();txDrops+=d.get_io$$txDrops();
    }
    void run(unsigned n){for(unsigned i=0;i<n;++i)cycle();}
    void until(const std::function<bool()>&f,unsigned limit=200000){for(unsigned i=0;i<limit&&!f();++i)cycle();
        if(!f()){std::cerr<<"commands="<<commands.size()<<" rxbeats="<<rx.size()<<" expectedTx="<<expectedTx.size()<<" expectedRx="<<expectedRx.size()<<'\n';
            throw std::runtime_error("Ethernet endpoint timeout");}}
    void drain(){until([&]{return commands.empty()&&rx.empty()&&expectedTx.empty()&&expectedRx.empty()&&expectedArpReplies.empty()&&wire.empty();});run(80);}
    void reset(){d.set_io$$rxValid(0);d.set_io$$inValid(0);d.set_io$$commandValid(0);d.set_io$$tick(0);d.set_io$$flush(0);
        d.set_io$$txReady(0);d.set_io$$outReady(0);d.set_reset(1);d.step();d.step();d.set_reset(0);}
};
int main(){try{
    Sim s;s.reset();
    // Software peer answers ARP. The first UDP datagram must be retained intact.
    s.queue(PEER,PEER_MAC,0,1);s.drain();check(s.probes==1&&s.txPackets==1,"cold ARP lost the first datagram");
    s.queue(0x08080808,GW_MAC,13,2);s.drain();check(s.probes==2,"off-link next hop did not use gateway ARP");
    // Simultaneous bidirectional data and ARP replies exercise packet arbitration.
    for(unsigned i=0;i<180;++i){unsigned n=i<40?i:s.rng()%701;uint32_t dst=i%4==0?0x08080808:PEER;
        s.queue(dst,dst==PEER?PEER_MAC:GW_MAC,n,10+i);
        Received r{PEER,LOCAL_IP,uint16_t(30000+i),uint16_t(40000+i),std::vector<uint8_t>(n)};for(auto&b:r.data)b=s.rng();
        s.expectedRx.push_back(r);s.inject(udp(PEER_MAC,LOCAL_MAC,r.src,r.dst,r.sp,r.dp,i,r.data));
        if(i%9==0)s.injectRequest(PEER_MAC,PEER);
    }
    s.drain();check(s.rxPackets==180&&s.txPackets==182&&s.txDrops==0&&s.rxDrops==0,"duplex packet accounting");
    // First data beat offered while stopped; a new ARP reply cannot preempt it.
    s.txReady=false;s.queue(PEER,PEER_MAC,201,300);s.until([&]{return bool(s.d.get_io$$txValid());});
    s.injectRequest(GW_MAC,GATEWAY);s.run(80);s.txReady=true;s.drain();
    // Broadcast and multicast mappings are direct and require no ARP lookup.
    unsigned probes=s.probes;
    s.queue(0xffffffff,BCAST,1,301);s.queue(0xc00002ff,BCAST,17,302);
    s.queue(0xe0010203,0x01005e010203ULL,27,303);s.drain();check(s.probes==probes,"ARP issued for direct link mapping");
    // Bad destination, unsupported EtherType, checksum failure, late FCS and oversize.
    unsigned oldRx=s.rxDrops;
    for(unsigned mode=0;mode<6;++mode){std::vector<uint8_t>data(mode==5?1100:64,0x5a);auto v=udp(PEER_MAC,LOCAL_MAC,PEER,LOCAL_IP,100,200,mode,data);
        if(mode==0)put(v,0,6,LOCAL_MAC+1);if(mode==1)put(v,12,2,0x86dd);if(mode==2)v[24]^=1;
        s.inject(v,mode==3,mode==4);
    }
    s.until([&]{return s.rxDrops==oldRx+6;});s.drain();
    // MTU and missing gateway fail locally; malformed payload never reaches MAC.
    unsigned oldTx=s.txDrops;s.mtu=100;s.queue(PEER,PEER_MAC,100,310,true);s.until([&]{return s.txDrops==oldTx+1;});s.drain();
    s.mtu=900;s.gateway=0;s.queue(0x08080404,0,1,311,true);s.until([&]{return s.txDrops==oldTx+2;});s.drain();s.gateway=GATEWAY;
    s.queue(PEER,PEER_MAC,50,312,true,true);s.until([&]{return s.txDrops==oldTx+3;});s.drain();
    // No timeout before actual wire transfer; three unanswered probes then drop.
    probes=s.probes;oldTx=s.txDrops;s.tick=true;s.txReady=false;s.queue(0xc00002ee,0,31,320,true);s.run(150);
    check(s.txDrops==oldTx&&s.probes==probes,"ARP timeout ran behind blocked physical TX");
    s.txReady=true;s.until([&]{return s.txDrops==oldTx+1;});s.tick=false;s.drain();
    check(s.probes==probes+3,"ARP retry count at shared MAC boundary");
    // Recovery after failed resolution, including a potentially expired cache.
    s.queue(PEER,PEER_MAC,33,321);s.drain();
    // Service-capacity measurement: continuous valid traffic, ready consumers,
    // both directions at once. These are cycle counts, not routed Fmax results.
    s.random=false;s.mtu=1500;
    unsigned caseIndex=0;
    for(unsigned size:{0U,1U,17U,512U,896U}){
        constexpr unsigned packets=256;size_t txStart=s.txTimes.size(),rxStart=s.rxTimes.size();
        unsigned beforeTxDrops=s.txDrops,beforeRxDrops=s.rxDrops;
        for(unsigned i=0;i<packets;++i){s.queue(PEER,PEER_MAC,size,1000+caseIndex*packets+i);
            Received r{PEER,LOCAL_IP,uint16_t(30000+i),uint16_t(40000+i),std::vector<uint8_t>(size)};
            for(auto&b:r.data)b=s.rng();s.expectedRx.push_back(r);s.inject(udp(PEER_MAC,LOCAL_MAC,PEER,LOCAL_IP,r.sp,r.dp,i,r.data));}
        s.drain();check(s.txTimes.size()==txStart+packets&&s.rxTimes.size()==rxStart+packets&&
            s.txDrops==beforeTxDrops&&s.rxDrops==beforeRxDrops,"service-capacity packet loss");
        double txCycles=double(s.txTimes.back()-s.txTimes[txStart])/(packets-1);
        double rxCycles=double(s.rxTimes.back()-s.rxTimes[rxStart])/(packets-1);
        double wireBits=8.0*(std::max(60U,42+size)+24); // FCS + preamble/SFD + IFG
        std::cout<<std::fixed<<std::setprecision(3)<<"ETHERNET_SERVICE payload="<<size<<" packets="<<packets
            <<" tx_cycles_per_packet="<<txCycles<<" rx_cycles_per_packet="<<rxCycles
            <<" tx_service_gbps_at_156_25mhz="<<wireBits*0.15625/txCycles
            <<" rx_service_gbps_at_156_25mhz="<<wireBits*0.15625/rxCycles<<'\n';++caseIndex;
    }
    std::cout<<"ETHERNET_UDP_ENDPOINT_PASS tx_datagrams="<<s.txPackets<<" rx_datagrams="<<s.rxPackets
        <<" arp_probes="<<s.probes<<" arp_replies="<<s.replies<<" tx_drops="<<s.txDrops<<" rx_drops="<<s.rxDrops
        <<" cycles="<<s.cycles<<" cold_lookup=retained arbitration=stable gateway=pass padding=pass\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
