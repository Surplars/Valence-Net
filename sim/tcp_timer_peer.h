#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>
static void check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
struct Event {
    bool tick=false,clear=false,tx=false,retransmit=false,syn=false,ack=false,handshake=false;
    uint32_t initial=0,end=0,ackno=0;
};
static bool after(uint32_t a,uint32_t b){uint32_t distance=a-b;return distance>0&&distance<0x80000000U;}
static bool covers(uint32_t a,uint32_t b){return a==b||after(a,b);}
struct Sample{uint32_t end;uint64_t start;bool ambiguous=false;};
// Independent absolute-deadline model. RTT equations use floating-point time
// and round onto a 1/256-tick grid; RTL uses fixed-point add/shift arithmetic.
template<bool Pipeline=false> struct Model{
    uint64_t now=0,deadline=0;uint32_t una=0,nxt=0;
    std::optional<unsigned> pendingRto;
    unsigned rto=10;bool running=false,expired=false,known=false,fallback=false;
    double srtt=0,variance=0;std::optional<Sample> sample;std::optional<uint32_t> synEnd;
    unsigned remaining()const{return running?unsigned(deadline-now):0;}
    bool newSend(const Event&e)const{return e.tx&&!e.retransmit&&after(e.end,nxt)&&after(e.end,una);}
    bool retransmission(const Event&e)const{return e.tx&&e.retransmit&&after(e.end,una)&&covers(nxt,e.end);}
    bool progress(const Event&e)const{return e.ack&&after(e.ackno,una)&&covers(newSend(e)?e.end:nxt,e.ackno);}
    bool completed(const Event&e)const{return sample&&progress(e)&&covers(e.ackno,sample->end);}
    bool sampled(const Event&e)const{return completed(e)&&!sample->ambiguous&&!retransmission(e);}
    void apply(const Event&e){
        uint64_t instant=now+e.tick;
        if(e.clear){*this=Model{};now=instant;una=nxt=e.initial;return;}
        bool fresh=newSend(e),ret=retransmission(e),ack=progress(e),done=completed(e),take=sampled(e);
        uint32_t sent=fresh?e.end:nxt,acked=ack?e.ackno:una;
        unsigned oldRto=rto;
        auto estimate=pendingRto;pendingRto.reset();
        if constexpr(Pipeline)if(estimate)rto=*estimate;
        bool restart=sent!=acked&&(ack||ret||(fresh&&!running&&!expired));
        bool timeout=e.tick&&running&&instant>=deadline&&!ack&&!ret&&(!Pipeline||(!take&&!estimate));
        if(take){
            double duration=std::min<uint64_t>(600,instant-sample->start+1);
            if(!known){srtt=duration;variance=duration/2;}
            else{
                double nextVariance=0.75*variance+0.25*std::abs(srtt-duration);
                double nextSrtt=0.875*srtt+0.125*duration;
                variance=std::ceil(nextVariance*256)/256;srtt=std::ceil(nextSrtt*256)/256;
            }
            known=true;unsigned calculated=std::clamp(unsigned(std::ceil(srtt+std::max(1.0,4*variance))),10U,600U);
            if constexpr(Pipeline)pendingRto=calculated;else rto=calculated;
        }
        if(e.handshake&&fallback)rto=std::max(rto,30U);
        if(ret&&sample)sample->ambiguous=true;
        if(done)sample.reset();
        if(fresh&&!sample&&after(e.end,acked))sample=Sample{e.end,instant};
        if(fresh&&e.syn)synEnd=e.end;
        if(ack&&synEnd&&covers(e.ackno,*synEnd))synEnd.reset();
        if(e.handshake)fallback=false;
        if(sent==acked){running=false;expired=false;}
        else if(Pipeline&&take){running=false;expired=false;}
        else if(restart||(Pipeline&&estimate)){running=true;expired=false;deadline=instant+rto+1;}
        else if(timeout){running=false;expired=true;rto=std::min(oldRto*2,600U);if(synEnd&&oldRto<30)fallback=true;}
        una=acked;nxt=sent;now=instant;
    }
};
template<class Harness,bool Pipeline=false> struct TimerPeer{
    static constexpr bool pipelined=Pipeline;
    Harness d;Model<Pipeline> m;unsigned cycles=0,samples=0,timeouts=0,invalidAcks=0,karn=0;
    TimerPeer(){d.set_io$$clear(0);d.set_io$$tick(0);d.set_io$$txValid(0);d.set_io$$ackValid(0);
        d.set_io$$handshakeComplete(0);d.set_reset(1);d.step();d.step();d.set_reset(0);}
    void step(Event e={}){
        d.set_io$$clear(e.clear);d.set_io$$initialSequence(e.initial);d.set_io$$tick(e.tick);
        d.set_io$$txValid(e.tx);d.set_io$$endSequence(e.end);d.set_io$$retransmission(e.retransmit);d.set_io$$syn(e.syn);
        d.set_io$$ackValid(e.ack);d.set_io$$ackSequence(e.ackno);d.set_io$$handshakeComplete(e.handshake);
        d.step();++cycles;
        if(d.get_io$$rto()!=m.rto||d.get_io$$remaining()!=m.remaining()){
            std::cerr<<"cycle="<<cycles<<" rto="<<d.get_io$$rto()<<" expected="<<m.rto
                <<" remaining="<<d.get_io$$remaining()<<" expected="<<m.remaining()<<'\n';
            throw std::runtime_error("timer deadline/RTO mismatch");}
        check(bool(d.get_io$$expired())==m.expired&&bool(d.get_io$$running())==m.running,"timer state mismatch");
        check(d.get_io$$oldest()==m.una&&d.get_io$$next()==m.nxt,"timer sequence ownership mismatch");
        check(bool(d.get_io$$rttKnown())==m.known,"RTT initialization mismatch");
        check(d.get_io$$srttFixed()==unsigned(m.srtt*256)&&d.get_io$$varianceFixed()==unsigned(m.variance*256),"RTT estimator mismatch");
        check(bool(d.get_io$$ackProgress())==(m.progress(e)&&!e.clear),"ACK acceptance mismatch");
        check(bool(d.get_io$$sampleTaken())==(m.sampled(e)&&!e.clear),"Karn sample eligibility mismatch");
        check(bool(d.get_io$$invalidTx())==(e.tx&&!m.newSend(e)&&!m.retransmission(e)&&!e.clear),"TX validation mismatch");
        if(m.sampled(e)&&!e.clear)++samples;
        if(m.completed(e)&&!m.sampled(e)&&!e.clear)++karn;
        if(e.ack&&!m.progress(e)&&!e.clear)++invalidAcks;
        bool wasExpired=m.expired;m.apply(e);if(m.expired&&!wasExpired)++timeouts;
    }
    void clear(uint32_t initial=0){Event e;e.clear=true;e.initial=initial;step(e);step();}
    void ticks(unsigned n){Event e;e.tick=true;while(n--)step(e);}
    void send(uint32_t end,bool ret=false,bool syn=false){Event e;e.tx=true;e.end=end;e.retransmit=ret;e.syn=syn;step(e);}
    void ack(uint32_t end,bool handshake=false){Event e;e.ack=true;e.ackno=end;e.handshake=handshake;step(e);}
    void expire(){check(m.running,"expire called without timer");ticks(m.remaining());step();check(m.expired,"deadline did not expire");}
};

template<class Test> int runTimerScenario(){try{
    Test t;t.clear(0xfffffff0U);t.send(0x10);t.ticks(3);
    t.ack(0xfffffff0U);t.ack(0x11);t.ack(0x80000010U);t.ack(0x0b);
    check(!t.m.known&&t.m.running&&t.m.remaining()==11,"partial ACK must restart without sampling");
    t.ticks(5);t.ack(0x10);t.step();
    check(t.m.known&&t.m.srtt==9&&t.m.variance==4.5&&t.m.rto==27&&!t.m.running,"first RTT estimate / wrap");
    // Subsequent RTT equations must use the OLD SRTT to update variance.
    t.send(0x20);t.ticks(1);t.ack(0x20);t.step();
    check(t.m.srtt==8.125&&t.m.variance==5.125&&t.m.rto==29,"subsequent RTT estimator ordering");
    // A progress ACK takes precedence over expiration on the same tick.
    t.clear();t.send(100);t.ticks(10);Event e;e.tick=true;e.ack=true;e.ackno=100;t.step(e);t.step();
    check(!t.m.expired&&!t.m.running&&t.m.known,"ACK/timeout priority");
    // Duplicate ACK does not postpone timeout; blocked retransmission backs off once.
    t.clear();t.send(100);t.ticks(10);e.ackno=0;t.step(e);t.ticks(200);t.step();
    check(t.m.expired&&t.m.rto==20&&!t.m.running,"duplicate ACK or repeated blocked backoff");
    t.send(200);t.step();check(t.m.expired,"new unrelated data must not cancel pending retransmission");
    t.send(100,true);t.ticks(3);t.ack(200);t.step();check(!t.m.known&&!t.m.running,"Karn ambiguity exclusion");
    t.send(300);t.ticks(2);t.ack(300);t.step();check(t.m.known&&t.m.rto==10,"fresh RTT collapses backed-off RTO");
    // SYN timeout changes the data-phase initial RTO to at least three seconds.
    t.clear(900);t.send(901,false,true);t.expire();t.send(901,true,true);t.ticks(1);t.ack(901,true);t.step();
    check(t.m.rto==30&&!t.m.known,"SYN retransmission data-phase RTO floor");
    t.send(1000);t.ticks(1);t.ack(1000);t.step();check(t.m.rto==10,"data RTT replaces SYN fallback");
    // Repeated losses saturate at sixty seconds, without integer wrap.
    t.clear();t.send(1,false,true);
    for(unsigned j=0;j<9;++j){t.expire();t.ticks(7);t.send(1,true,true);}
    check(t.m.rto==600,"RTO maximum");t.ack(1,true);t.step();
    // A very late original ACK may still supply a sample; no retransmission happened.
    t.clear();t.send(100);t.ticks(1200);t.ack(100);t.step();
    check(t.m.known&&t.m.srtt==600&&t.m.rto==600,"saturated long RTT sample");
    // One sample completes while the next transmitted segment starts another.
    t.clear();t.send(100);t.ticks(2);e=Event{};e.tx=true;e.end=200;e.ack=true;e.ackno=100;e.tick=true;
    t.step(e);t.ticks(2);t.ack(200);t.step();check(t.m.known&&!t.m.running,"simultaneous ACK/new transmission");
    // Clearing a live/expired connection discards its sample and sequence ownership.
    t.clear();t.send(200);t.expire();t.clear(0xfffffffeU);t.send(2);t.ack(2);t.step();
    check(t.m.known&&t.m.rto==10&&!t.m.expired,"connection reuse / clear");
    // Batched completion and ACK events must not leave a phantom outstanding SYN.
    t.clear();e=Event{};e.tx=true;e.end=1;e.syn=true;e.ack=true;e.ackno=1;t.step(e);
    t.send(2);t.expire();t.send(2,true);t.ack(2,true);t.step();
    check(t.m.rto==20,"acknowledged SYN incorrectly triggered handshake fallback");
    std::mt19937_64 rng(0x629892937323ULL);
    for(unsigned i=0;i<80000;++i){
        Event x;x.tick=rng()%4!=0;
        if(i%2000==0){x.clear=true;x.initial=uint32_t(0xfffff000U+rng()%4096);}
        else{
            unsigned phase=i%2000;
            if(rng()%12==0){
                x.tx=true;x.retransmit=rng()%3==0;
                if(x.retransmit)x.end=t.m.nxt;
                else x.end=t.m.nxt+1+rng()%1500;
                if(rng()%16==0)x.end=t.m.una; // Rejected send event.
            }
            if(phase<600||phase>1500){
                if(rng()%5==0){
                    x.ack=true;
                    switch(rng()%6){
                        case 0:x.ackno=t.m.una;break;
                        case 1:x.ackno=t.m.nxt+500;break;
                        case 2:x.ackno=t.m.una-1;break;
                        case 3:x.ackno=t.m.una+(t.m.nxt-t.m.una)/2;break;
                        default:x.ackno=t.m.nxt;break;
                    }
                }
            }
        }
        t.step(x);
    }
    t.clear();t.step();
    check(t.samples>500&&t.timeouts>20&&t.invalidAcks>1000&&t.karn>20,"timer randomized coverage");
    std::cout<<(Test::pipelined?"TCP_PIPELINED_TIMER_PASS cycles=":"TCP_RETRANSMISSION_TIMER_PASS cycles=")<<t.cycles<<" samples="<<t.samples<<" timeouts="<<t.timeouts
        <<" rejected_acks="<<t.invalidAcks<<" karn_exclusions="<<t.karn
        <<" wrap=pass backoff=pass syn_fallback=pass deadline_priority=pass\n";
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
