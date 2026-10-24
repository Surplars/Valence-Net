#include "MultiEthernetPortHarness.h"
#include "multi_tcp_peer.h"
using Peer=MultiTcpPeer<SMultiEthernetPortHarness,true>;

static void gsimInitializationRegression(){
    using Model=SMultiEthernetPortHarness;
    Model model;
    constexpr auto first=offsetof(Model,_var_start),last=offsetof(Model,_var_end);
    auto* bytes=reinterpret_cast<unsigned char*>(&model);
    std::memset(bytes+first,0xa5,last-first);
    model.init();
    check(bytes[first]==0&&bytes[last-1]==0xa5,"pinned GSIM partial-clear defect changed; review initialization workaround");
    const auto cycles=model.cycles,logStart=model.LOG_START,logEnd=model.LOG_END;
    initializeGsimModel(model,&Model::init);
    check(std::all_of(bytes+first,bytes+last,[](unsigned char b){return b==0;}),
        "GSIM initialization left model state uncleared");
    check(model.cycles==cycles&&model.LOG_START==logStart&&model.LOG_END==logEnd,"GSIM helper modified control fields");
    model.set_reset(1);model.step();model.step();model.set_reset(0);model.step();
    std::cout<<"GSIM initialization: poisoned storage, pinned defect and complete byte clear passed\n";
}

static std::vector<uint8_t> ipPacket(unsigned protocol,const std::vector<uint8_t>&payload,
    uint32_t src=0x0a000002,uint32_t dst=0x0a000001,unsigned id=7){
    std::vector<uint8_t>v(20);v[0]=0x45;put16(v,2,20+payload.size());put16(v,4,id);
    put16(v,6,0x4000);v[8]=64;v[9]=protocol;put32(v,12,src);put32(v,16,dst);put16(v,10,sum(v));
    v.insert(v.end(),payload.begin(),payload.end());return v;
}
static void repairIp(std::vector<uint8_t>&v){put16(v,10,0);put16(v,10,sum({v.begin(),v.begin()+20}));}
static void repairTransport(std::vector<uint8_t>&v){
    unsigned field=v[9]==6?36:26;put16(v,field,0);
    std::vector<uint8_t>pseudo(v.begin()+12,v.begin()+20);unsigned len=u16(v,2)-20;
    pseudo.push_back(0);pseudo.push_back(v[9]);pseudo.push_back(len>>8);pseudo.push_back(len);
    pseudo.insert(pseudo.end(),v.begin()+20,v.begin()+u16(v,2));
    uint16_t c=sum(pseudo);put16(v,field,(!c&&v[9]==17)?65535:c);
}
static std::vector<uint8_t> udpPacket(unsigned destinationPort,const std::vector<uint8_t>&payload,
    uint32_t src=0x0a000002,uint32_t dst=0x0a000001){
    std::vector<uint8_t>udp(8);put16(udp,0,9000);put16(udp,2,destinationPort);put16(udp,4,8+payload.size());
    udp.insert(udp.end(),payload.begin(),payload.end());auto v=ipPacket(17,udp,src,dst);
    repairTransport(v);return v;
}
static std::vector<uint8_t> icmpError(const std::vector<uint8_t>&original,
    unsigned type=3,unsigned code=4,unsigned mtu=600,uint32_t router=0x0a0000fe){
    check(original.size()>=28,"test quotation must have IP header and transport prefix");
    std::vector<uint8_t>icmp(8);icmp[0]=type;icmp[1]=code;
    if(type==3&&code==4)put16(icmp,6,mtu);
    if(type==12)icmp[4]=9;
    icmp.insert(icmp.end(),original.begin(),original.begin()+28);
    put16(icmp,2,sum(icmp));return ipPacket(1,icmp,router);
}
static void refill(Peer&p){p.idle(12,true);p.idle(50);}
static void expectNoReply(Peer&p,const std::vector<uint8_t>&ip,bool linkBroadcast=false){
    refill(p);auto count=p.otherPackets.size();auto frame=p.wrap(ip);
    if(linkBroadcast)std::fill(frame.begin(),frame.begin()+6,255);
    p.rawWire(frame);p.idle(250);check(p.otherPackets.size()==count,"suppressed packet produced an exception reply");
}
static void checkQuote(const std::vector<uint8_t>&reply,const std::vector<uint8_t>&original,unsigned code){
    unsigned n=std::min(28U,unsigned(original.size()));
    check(reply[9]==1&&reply[20]==3&&reply[21]==code&&reply.size()==28+n,"ICMP unreachable type/code/quote length");
    check(u32(reply,12)==0x0a000001&&u32(reply,16)==u32(original,12),"ICMP unreachable address reversal");
    check(std::equal(original.begin(),original.begin()+n,reply.begin()+28),"ICMP original quote changed or included padding");
}

static void resetAndUnreachable(){
    Peer p;p.random=false;p.learn(0);
    auto syn=p.encode(0,0xffffffffU,0,2);put16(syn,22,6000);repairTransport(syn);
    p.wire(syn);p.idle(250);
    check(p.otherPackets.size()==1&&p.otherPackets.back()[9]==6,"unknown Ethernet TCP tuple did not reset");
    auto&r=p.otherPackets.back();check(r[33]==20&&u32(r,24)==0&&u32(r,28)==0&&u16(r,20)==6000,
        "Ethernet reset fields");
    check(p.d.get_io$$outstanding()==0&&p.pending.empty()&&p.packets.empty(),"stateless reset entered TCP completion ledger");
    auto bad=syn;bad[36]^=1;expectNoReply(p,bad);
    auto rst=syn;rst[33]=4;repairTransport(rst);expectNoReply(p,rst);
    expectNoReply(p,syn,true);

    auto unknown=ipPacket(47,{1,2,3,4,5,6,7,8,9,10,11});
    refill(p);auto count=p.otherPackets.size();p.wire(unknown);p.idle(250);
    check(p.otherPackets.size()==count+1,"unknown IP protocol missing ICMP");checkQuote(p.otherPackets.back(),unknown,2);
    auto shortUnknown=ipPacket(47,{0xa5});
    refill(p);count=p.otherPackets.size();p.wire(shortUnknown);p.idle(250);
    check(p.otherPackets.size()==count+1,"short unknown protocol missing ICMP");checkQuote(p.otherPackets.back(),shortUnknown,2);
    bad=unknown;bad[10]^=1;expectNoReply(p,bad);
    bad=unknown;put16(bad,6,0x2000);repairIp(bad);expectNoReply(p,bad);
    expectNoReply(p,unknown,true);
    expectNoReply(p,ipPacket(47,{1,2,3,4,5,6,7,8},0x0a0000ff));
    expectNoReply(p,ipPacket(47,{1,2,3,4,5,6,7,8},0xe0000001));
    // An incoming ICMP error never produces another ICMP error.
    expectNoReply(p,icmpError(syn,3,1,0));

    auto accepted=udpPacket(1300,p.pattern(79));
    count=p.otherPackets.size();p.wire(accepted);p.idle(250);
    check(p.udpBytes==std::vector<uint8_t>(accepted.begin()+28,accepted.end())&&p.otherPackets.size()==count,
        "bound UDP port did not deliver exact payload");
    auto closed=udpPacket(1301,p.pattern(87));
    refill(p);count=p.otherPackets.size();p.wire(closed);p.idle(250);
    check(p.otherPackets.size()==count+1,"unbound UDP port missing ICMP");checkQuote(p.otherPackets.back(),closed,3);
    check(p.udpBytes.size()==79,"closed UDP datagram leaked to application");
    auto zeroChecksum=closed;put16(zeroChecksum,26,0);
    refill(p);count=p.otherPackets.size();p.wire(zeroChecksum);p.idle(250);
    check(p.otherPackets.size()==count+1,"legal zero UDP checksum rejected");checkQuote(p.otherPackets.back(),zeroChecksum,3);
    bad=closed;bad.back()^=1;expectNoReply(p,bad);
    bad=closed;put16(bad,24,u16(bad,24)-1);repairTransport(bad);expectNoReply(p,bad);
    expectNoReply(p,closed,true);
    expectNoReply(p,udpPacket(1301,{1,2,3},0x0a000002,0xffffffffU),true);
    // Backpressure preserves complete exception bytes and does not expose errors.
    refill(p);p.stallOutput=true;p.wire(unknown);p.idle(200);
    check(p.d.get_io$$outValid(),"exception backpressure not exercised");
    p.stallOutput=false;p.idle(250);checkQuote(p.otherPackets.back(),unknown,2);
    std::cout<<"ethernet exception: reset ownership, ICMP quoting, UDP binding/checksum and broadcast suppression passed\n";
}

static void tcpPmtu(){
    Peer p;p.random=false;p.learn(0);p.learn(1);
    p.active(0,0xffffffe0U,200000,1460);p.active(1,300000,400000,1460);p.select(0);
    check(p.d.get_io$$pathMtu()==1500&&p.d.get_io$$sendMss()==1460,"initial TCP PMTU/MSS");
    p.write(0,1460);p.until([&]{return p.dataPackets[0]>=1;});p.drain();
    auto it=std::find_if(p.packets.rbegin(),p.packets.rend(),[](const auto&x){return x.meta.connection==0&&x.meta.data;});
    check(it!=p.packets.rend()&&it->bytes.size()==1500,"large retained PMTU test packet");
    auto original=it->bytes;uint32_t begin=u32(original,24),end=begin+1460;
    auto first=p.packets.size();auto notices=p.tcpErrors;
    // Valid checksums alone do not authorize a transport state change.
    auto quote=original;put32(quote,24,end+1);p.wire(icmpError(quote));p.idle(250);
    check(p.d.get_io$$pathMtu()==1500&&p.tcpErrors==notices,"out-of-flight ICMP quote changed PMTU");
    quote=original;put16(quote,20,u16(quote,20)+1);p.wire(icmpError(quote));p.idle(250);
    check(p.d.get_io$$pathMtu()==1500&&p.tcpErrors==notices,"wrong-tuple ICMP quote changed PMTU");
    quote=original;quote[10]^=1;p.wire(icmpError(quote));p.idle(250);
    check(p.d.get_io$$pathMtu()==1500&&p.tcpErrors==notices,"bad embedded IPv4 checksum changed PMTU");
    auto invalid=icmpError(original);invalid[22]^=1;p.wire(invalid);p.idle(250);
    check(p.d.get_io$$pathMtu()==1500&&p.tcpErrors==notices,"bad ICMP checksum changed PMTU");
    p.wire(icmpError(original,3,4,67));p.idle(250);
    p.wire(icmpError(original,3,4,1600));p.idle(250);
    quote=original;put16(quote,6,0);repairIp(quote);p.wire(icmpError(quote));p.idle(250);
    check(p.d.get_io$$pathMtu()==1500&&p.tcpErrors==notices,"invalid MTU or non-DF quote changed PMTU");
    auto frame=p.wrap(icmpError(original));std::fill(frame.begin(),frame.begin()+6,255);
    p.rawWire(frame);p.idle(250);
    check(p.d.get_io$$pathMtu()==1500&&p.tcpErrors==notices,"link-broadcast ICMP changed PMTU");
    // A matching soft network error is reported but does not abort the session.
    p.wire(icmpError(original,11,0,0));p.idle(250);
    check(p.tcpErrors==notices+1&&p.state(0)==4&&p.d.get_io$$pathMtu()==1500,"time-exceeded error policy");
    p.wire(icmpError(original,12,0,0));p.idle(250);
    check(p.tcpErrors==notices+2&&p.state(0)==4,"parameter-problem error policy");
    p.wire(icmpError(original));p.idle(300);
    check(p.d.get_io$$pathMtu()==600&&p.d.get_io$$sendMss()==560&&p.lastTcpErrorMtu==600,
        "valid Ethernet ICMP did not lower PMTU/MSS");
    check(p.d.get_io$$sendNext()==end,"PMTU update allocated new sequence space");
    p.select(1);check(p.d.get_io$$pathMtu()==1500&&p.d.get_io$$sendMss()==1460&&p.state(1)==4,"PMTU leaked to another TCP context");
    p.select(0);
    // ACK only bytes received in the small repairs, never the discarded original.
    uint32_t cursor=begin;unsigned scan=first,repairs=0;
    while(cursor!=end){
        unsigned found=0;
        p.until([&]{
            for(unsigned i=scan;i<p.packets.size();++i){const auto&x=p.packets[i];
                if(x.meta.connection==0&&x.meta.data&&x.meta.retry&&u32(x.bytes,24)==cursor){found=i;return true;}}
            return false;
        });
        auto x=p.packets[found];scan=found+1;
        unsigned header=20+(x.bytes[32]>>4)*4,n=x.bytes.size()-header;
        check(n>0&&x.bytes.size()<=600&&uint32_t(x.meta.end-cursor)==n,"PMTU retained repair size/sequence");
        cursor=x.meta.end;++repairs;check(repairs<=4,"PMTU repair did not make progress");
        p.rx(0,p.peerNext[0],cursor);
    }
    p.drain();p.select(0);
    check(repairs==3&&p.d.get_io$$freeBanks()==4&&p.d.get_io$$sendOldest()==end,
        "PMTU repair did not preserve/retire the full retained payload");
    notices=p.tcpErrors;p.wire(icmpError(original,3,4,296));p.idle(250);
    check(p.d.get_io$$pathMtu()==600&&p.tcpErrors==notices,"stale cumulatively ACKed quote changed PMTU");
    unsigned sent=p.dataPackets[0];first=p.packets.size();p.write(0,1000);
    p.until([&]{return p.dataPackets[0]>=sent+2;});p.drain();
    for(unsigned i=first;i<p.packets.size();++i)if(p.packets[i].meta.data&&p.packets[i].meta.connection==0)
        check(p.packets[i].bytes.size()<=600,"new TCP data ignored learned PMTU");
    p.ack(0);p.drain();
    sent=p.dataPackets[1];p.write(1,1460);p.until([&]{return p.dataPackets[1]>sent;});p.drain();
    check(p.packets.back().meta.data&&p.packets.back().meta.connection==1&&p.packets.back().bytes.size()==1500,
        "healthy context lost its original segment budget");p.ack(1);p.drain();
    std::cout<<"ethernet TCP PMTU: quote validation, soft errors, 1500->600, wrapped retained repairs and per-context isolation passed\n";
}

static void sendUdp(Peer&p,unsigned n,unsigned id=0x1234,unsigned connection=0){
    p.d.set_io$$udpTxDestination(p.remoteIp[connection]);p.d.set_io$$udpTxSourcePort(1300);
    p.d.set_io$$udpTxDestinationPort(9000);p.d.set_io$$udpTxLength(n);p.d.set_io$$udpTxIdentification(id);
    p.d.set_io$$udpCommandValid(1);p.until([&]{return p.d.get_io$$udpCommandReady();});
    p.d.set_io$$udpCommandValid(0);
    for(unsigned off=0;off<n||off==0;off+=16){
        unsigned count=std::min(16U,n-off);uint64_t lo=0,hi=0;
        for(unsigned i=0;i<count;++i)(i<8?lo:hi)|=uint64_t(uint8_t((off+i)*11+5))<<(8*(i%8));
        p.d.set_io$$udpDataLo(lo);p.d.set_io$$udpDataHi(hi);p.d.set_io$$udpDataKeep((1U<<count)-1);
        p.d.set_io$$udpDataLast(off+count==n);p.d.set_io$$udpDataValid(1);
        p.until([&]{return p.d.get_io$$udpDataReady();});p.d.set_io$$udpDataValid(0);if(!n)break;
    }
    p.idle(300);
}
static void udpPmtu(){
    Peer p;p.random=false;p.learn(0);p.learn(1);sendUdp(p,1000);
    check(p.otherPackets.size()==1&&p.otherPackets.back()[9]==17&&p.otherPackets.back().size()==1028,"UDP PMTU original send");
    auto original=p.otherPackets.back();auto notices=p.udpErrors;
    auto quote=original;put16(quote,4,u16(quote,4)+1);repairIp(quote);
    p.wire(icmpError(quote));p.idle(200);
    quote=original;quote[27]^=1;p.wire(icmpError(quote));p.idle(200);
    check(p.udpErrors==notices,"forged UDP recent-send identity accepted");
    p.wire(icmpError(original));p.idle(200);
    check(p.udpErrors==notices+1&&p.udpLocalErrors==0&&p.lastUdpErrorMtu==600,"valid UDP PMTU error not reported");
    auto count=p.otherPackets.size();sendUdp(p,700,0x1235);
    check(p.otherPackets.size()==count&&p.udpLocalErrors==1&&p.lastUdpErrorMtu==600,
        "oversized UDP not locally rejected/drained against PMTU");
    sendUdp(p,572,0x1236);
    check(p.otherPackets.size()==count+1&&p.otherPackets.back().size()==600,"UDP exact PMTU boundary did not send");
    auto&v=p.otherPackets.back();for(unsigned i=0;i<572;++i)check(v[28+i]==uint8_t(i*11+5),"UDP payload corrupted after rejected datagram");
    sendUdp(p,1000,0x1237,1);
    check(p.otherPackets.size()==count+2&&p.otherPackets.back().size()==1028&&u32(p.otherPackets.back(),16)==p.remoteIp[1],
        "UDP PMTU limit leaked to another destination");
    check(p.d.get_io$$outstanding()==0&&p.pending.empty(),"UDP errors modified TCP completion ledger");
    std::cout<<"ethernet UDP PMTU: exact recent quote, local length error, payload drain, boundary and destination isolation passed\n";
}
int main(){try{
    gsimInitializationRegression();resetAndUnreachable();tcpPmtu();udpPmtu();
    std::cout<<"ETHERNET_ERRORS_PASS tcp_reset=pass unreachable_udp_ports=pass error_validation=pass "
        <<"tcp_pmtu_repair=pass udp_pmtu_limit=pass broadcast_suppression=pass\n";return 0;
}catch(const std::exception&e){std::cerr<<"ethernet_errors: "<<e.what()<<'\n';return 1;}}
