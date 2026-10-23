#pragma once
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>
#include <vector>
static void check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
enum State{Closed,Listen,SynSent,SynReceived,Established,FinWait1,FinWait2,CloseWait,Closing,LastAck,TimeWait};
struct Packet{
    uint32_t generation,src,dst,seq,ack;unsigned sport,dport,flags,window,optlen;
    uint64_t options;bool retransmission,sequenceSpace;
    bool operator==(const Packet&)const=default;
};
struct Decision{
    uint32_t generation,token;unsigned skip,length;bool keep,fin,early,urgent;uint32_t urgentSequence;
    bool operator==(const Decision&)const=default;
};
struct Ack{
    uint32_t generation,previous,number,window;bool duplicate,changed,handshake;
    bool operator==(const Ack&)const=default;
};
struct Notice{uint32_t generation;unsigned flags;bool operator==(const Notice&)const=default;};
struct Segment{
    uint32_t seq=0,ack=0;unsigned flags=16,length=0,window=5000,errors=0,urgent=0;
    bool stored=true,mssPresent=false,scalePresent=false;unsigned mss=1460,scale=0;
    uint32_t src=0x0a000002,dst=0x0a000001;unsigned sport=5000,dport=4000;
};
template<typename Harness>
struct TcpControlPeer{
    Harness d;std::mt19937_64 rng{0x929359617323ULL};
    std::vector<Packet> packets;std::vector<Decision> decisions;std::vector<Ack> acks;std::vector<Notice> notices;std::vector<uint32_t> replays,timeouts;
    std::optional<Packet> heldPacket;std::optional<Decision> heldDecision;std::optional<Ack> heldAck;std::optional<Notice> heldNotice;
    std::optional<uint32_t> heldReplay;bool stall=false,stallAck=false;uint32_t token=100,epoch=0,iss=0;
    unsigned cycles=0,states=0,reservations=0,receives=0,completed=0;
    Packet packet(){return {d.get_io$$controlGeneration(),d.get_io$$txSourceIp(),d.get_io$$txDestinationIp(),
        d.get_io$$txSequence(),d.get_io$$txAcknowledgement(),d.get_io$$txSourcePort(),d.get_io$$txDestinationPort(),
        d.get_io$$txFlags(),d.get_io$$txWindow(),d.get_io$$txOptionLength(),d.get_io$$txOptions(),
        bool(d.get_io$$controlRetransmission()),bool(d.get_io$$controlSequenceSpace())};}
    Decision decision(){return {d.get_io$$dispositionGeneration(),d.get_io$$dispositionToken(),d.get_io$$dispositionSkip(),
        d.get_io$$dispositionLength(),bool(d.get_io$$dispositionKeep()),bool(d.get_io$$dispositionFin()),
        bool(d.get_io$$dispositionBeforeEstablished()),bool(d.get_io$$dispositionUrgent()),d.get_io$$dispositionUrgentSequence()};}
    Ack ack(){return {d.get_io$$ackGeneration(),d.get_io$$ackPrevious(),d.get_io$$ackAcknowledgement(),d.get_io$$ackWindow(),
        bool(d.get_io$$ackDuplicate()),bool(d.get_io$$ackWindowChanged()),bool(d.get_io$$ackHandshake())};}
    Notice notice(){return {d.get_io$$noticeGeneration(),d.get_io$$noticeFlags()};}
    TcpControlPeer(){
        d.set_io$$openValid(0);d.set_io$$closeValid(0);d.set_io$$rxValid(0);d.set_io$$reserveValid(0);
        d.set_io$$completedValid(0);if constexpr(requires{d.set_io$$retryValid(0);})d.set_io$$retryValid(0);d.set_io$$tick(0);
        d.set_io$$controlReady(0);d.set_io$$dispositionReady(0);d.set_io$$ackReady(0);d.set_io$$noticeReady(0);d.set_io$$replayReady(0);
        d.set_io$$updateWindow(0);
        d.set_io$$receiveWindow(4096);d.set_io$$congestionWindow(65535);
        d.set_reset(1);d.step();d.step();d.set_reset(0);step();
    }
    void step(bool tick=false){
        bool cp=!stall&&rng()%4!=0,dp=!stall&&rng()%3!=0,ap=!stall&&!stallAck&&rng()%4!=0,np=!stall&&rng()%3!=0,rp=!stall&&rng()%4!=0;
        d.set_io$$controlReady(cp);d.set_io$$dispositionReady(dp);d.set_io$$ackReady(ap);d.set_io$$noticeReady(np);d.set_io$$replayReady(rp);
        d.set_io$$tick(tick);d.step();++cycles;states|=1U<<d.get_io$$state();
        if(heldPacket)check(d.get_io$$controlValid()&&packet()==*heldPacket,"control packet changed under backpressure");
        if(heldDecision)check(d.get_io$$dispositionValid()&&decision()==*heldDecision,"RX disposition changed under backpressure");
        if(heldAck)check(d.get_io$$ackValid()&&ack()==*heldAck,"ACK event changed under backpressure");
        if(heldNotice)check(d.get_io$$noticeValid()&&notice()==*heldNotice,"notice changed under backpressure");
        if(heldReplay)check(d.get_io$$replayValid()&&d.get_io$$replaySequence()==*heldReplay,"replay event changed under backpressure");
        heldPacket=d.get_io$$controlValid()&&!cp?std::optional<Packet>(packet()):std::nullopt;
        heldDecision=d.get_io$$dispositionValid()&&!dp?std::optional<Decision>(decision()):std::nullopt;
        heldAck=d.get_io$$ackValid()&&!ap?std::optional<Ack>(ack()):std::nullopt;
        heldNotice=d.get_io$$noticeValid()&&!np?std::optional<Notice>(notice()):std::nullopt;
        heldReplay=d.get_io$$replayValid()&&!rp?std::optional<uint32_t>(d.get_io$$replaySequence()):std::nullopt;
        if(d.get_io$$controlValid()&&cp){auto p=packet();check(p.sequenceSpace==bool(p.flags&3),"control sequence-space classification");packets.push_back(p);}
        if(d.get_io$$dispositionValid()&&dp)decisions.push_back(decision());
        if(d.get_io$$ackValid()&&ap)acks.push_back(ack());
        if(d.get_io$$noticeValid()&&np)notices.push_back(notice());
        if(d.get_io$$replayValid()&&rp)replays.push_back(d.get_io$$replaySequence());
        if constexpr(requires{d.get_io$$timeoutValid();})if(d.get_io$$timeoutValid())timeouts.push_back(d.get_io$$timeoutGeneration());
    }
    void drain(){unsigned quiet=0;for(unsigned i=0;i<500;++i){step();
        bool busy=d.get_io$$controlValid()||d.get_io$$dispositionValid()||d.get_io$$ackValid()||d.get_io$$noticeValid()||d.get_io$$replayValid();
        quiet=busy?0:quiet+1;if(quiet==3)return;}throw std::runtime_error("control outputs deadlocked");}
    void ticks(unsigned n){while(n--)step(true);drain();}
    void expectState(State s){if(d.get_io$$state()!=s){std::cerr<<"cycle="<<cycles<<" state="<<unsigned(d.get_io$$state())<<" want="<<s<<'\n';throw std::runtime_error("TCP state transition");}}
    void open(bool passive,uint32_t initial,bool scale=false,unsigned localScale=0,unsigned mss=1460){
        expectState(Closed);iss=initial;
        d.set_io$$passive(passive);d.set_io$$localIp(0x0a000001);d.set_io$$remoteIp(passive?0:0x0a000002);
        d.set_io$$localPort(4000);d.set_io$$remotePort(passive?0:5000);d.set_io$$initialSequence(initial);
        d.set_io$$passiveInitialSequence(initial);d.set_io$$localMss(mss);d.set_io$$offerScale(scale);d.set_io$$localScale(localScale);
        d.set_io$$openValid(1);unsigned guard=0;do{step();check(++guard<500,"OPEN stalled");}while(!d.get_io$$openReady());
        d.set_io$$openValid(0);drain();epoch=d.get_io$$generation();expectState(passive?Listen:SynSent);
        if(!passive){mss=std::clamp(mss,1U,65495U);auto&p=packets.back();check(p.flags==2&&p.seq==initial&&p.ack==0&&p.generation==epoch,"active SYN");
            check(p.optlen==(scale?8U:4U)&&uint32_t(p.options)==(2U|4U<<8|(mss>>8)<<16|(mss&255)<<24),"SYN MSS encoding");
            if(scale)check(uint32_t(p.options>>32)==(1U|3U<<8|3U<<16|std::min(localScale,14U)<<24),"SYN WS encoding");
            check(d.get_io$$sendActual()==initial&&d.get_io$$sendNext()==initial+1,"queued SYN advanced actual wire sequence");}
    }
    void finish(uint32_t end,bool retrans=false,std::optional<uint32_t> generation={},bool valid=true,std::optional<bool> invalid={}){
        d.set_io$$completedGeneration(generation.value_or(epoch));d.set_io$$completedEnd(end);d.set_io$$completedRetransmission(retrans);
        d.set_io$$completedValid(1);step();check(bool(d.get_io$$completionAccepted())==valid,"wire completion acceptance");
        if(invalid)check(bool(d.get_io$$invalidCompletion())==*invalid,"wire completion invalid classification");
        d.set_io$$completedValid(0);drain();if(valid)++completed;
    }
    void finish(const Packet&p){check(p.sequenceSpace,"attempt to complete ACK-only packet");finish(p.seq+bool(p.flags&2)+bool(p.flags&1),p.retransmission,p.generation);}
    void rx(Segment s,bool drainOutputs=true,bool arrivalTick=false){
        d.set_io$$rxSourceIp(s.src);d.set_io$$rxDestinationIp(s.dst);d.set_io$$rxSourcePort(s.sport);d.set_io$$rxDestinationPort(s.dport);
        d.set_io$$rxSequence(s.seq);d.set_io$$rxAcknowledgement(s.ack);d.set_io$$rxFlags(s.flags);
        d.set_io$$rxWindow(s.window);d.set_io$$rxLength(s.length);d.set_io$$rxErrors(s.errors);d.set_io$$rxUrgent(s.urgent);
        d.set_io$$rxMssPresent(s.mssPresent);d.set_io$$rxMss(s.mss);d.set_io$$rxScalePresent(s.scalePresent);d.set_io$$rxScale(s.scale);
        d.set_io$$payloadStored(s.stored);d.set_io$$payloadToken(++token);d.set_io$$rxValid(1);
        size_t before=decisions.size();unsigned guard=0;do{step(arrivalTick);check(++guard<500,"RX metadata stalled");}while(!d.get_io$$rxReady());
        d.set_io$$rxValid(0);++receives;
        if(drainOutputs){drain();check(decisions.size()==before+1&&decisions.back().token==token,"one disposition per segment");}
    }
    void close(bool abort=false){
        d.set_io$$abort(abort);d.set_io$$closeValid(1);unsigned guard=0;
        do{step();check(++guard<500,"CLOSE stalled");}while(!d.get_io$$closeReady());d.set_io$$closeValid(0);drain();
    }
    void reserve(unsigned n,bool allowed=true){
        uint32_t before=d.get_io$$sendNext();d.set_io$$reserveLength(n);d.set_io$$reserveValid(1);step();
        check(bool(d.get_io$$reserveReady())==allowed,"send window/MSS/congestion reservation");
        if(allowed)check(d.get_io$$sendSequence()==before,"reserved sequence start");
        d.set_io$$reserveValid(0);drain();check(d.get_io$$sendNext()==before+(allowed?n:0),"reservation changed wrong sequence");
        if(allowed)++reservations;
    }
    void retry(std::optional<uint32_t> generation={}){
        d.set_io$$retryGeneration(generation.value_or(epoch));d.set_io$$retryValid(1);unsigned guard=0;
        do{step();check(++guard<500,"retry stalled");}while(!d.get_io$$retryReady());if constexpr(requires{d.set_io$$retryValid(0);})d.set_io$$retryValid(0);drain();
    }
    void active(uint32_t initial,uint32_t peer,bool scale=false,unsigned localScale=0,unsigned peerScale=0){
        open(false,initial,scale,localScale);finish(packets.back());
        Segment s;s.seq=peer;s.ack=initial+1;s.flags=18;s.scalePresent=scale;s.scale=peerScale;s.mssPresent=true;s.mss=1200;
        rx(s);expectState(Established);check(d.get_io$$sendWindow()==5000,"SYN window incorrectly scaled");
        check(d.get_io$$sendMss()==1200&&d.get_io$$receiveNext()==peer+1,"handshake negotiation");
    }
    void passive(uint32_t initial,uint32_t peer,bool scale=false,unsigned localScale=0,unsigned peerScale=0){
        open(true,initial,scale,localScale);
        Segment s;s.seq=peer;s.flags=2;s.scalePresent=scale;s.scale=peerScale;s.mssPresent=true;s.mss=1300;
        rx(s);expectState(SynReceived);finish(packets.back());
        s.seq=peer+1;s.ack=initial+1;s.flags=16;s.scalePresent=false;s.mssPresent=false;rx(s);expectState(Established);
    }
    void abort(){close(true);expectState(Closed);}
};
