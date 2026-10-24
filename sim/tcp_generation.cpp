#include "TcpGenerationHarness.h"
#include "gsim_model_init.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>
static void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
struct Probe {
    STcpGenerationHarness d;
    unsigned cycles=0,gaps=0,enabled=0,options=0,ackGaps=0,countGaps=0,opens=0,rollbacks=0;
    uint32_t sequence=1000;
    Probe(){
        initializeGsimModel(d,&STcpGenerationHarness::init);
        d.set_io$$openValid(0);d.set_io$$closeValid(0);d.set_io$$rxValid(0);
        d.set_io$$reserveValid(0);d.set_io$$completedValid(0);d.set_io$$retryValid(0);
        d.set_io$$updateWindow(0);d.set_io$$tick(0);d.set_io$$abort(0);
        d.set_io$$controlReady(1);d.set_io$$dispositionReady(1);d.set_io$$ackReady(1);
        d.set_io$$noticeReady(1);d.set_io$$replayReady(1);
        d.set_io$$localIp(0x0a000001);d.set_io$$remoteIp(0x0a000002);
        d.set_io$$localPort(4000);d.set_io$$remotePort(5000);
        d.set_io$$localMss(1460);d.set_io$$offerScale(0);d.set_io$$localScale(0);
        d.set_io$$receiveWindow(65535);d.set_io$$congestionWindow(65535);
        d.set_io$$rxSourceIp(0x0a000002);d.set_io$$rxDestinationIp(0x0a000001);
        d.set_io$$rxSourcePort(5000);d.set_io$$rxDestinationPort(4000);
        d.set_io$$rxWindow(5000);d.set_io$$rxErrors(0);d.set_io$$rxUrgent(0);
        d.set_io$$rxMssPresent(1);d.set_io$$rxMss(1460);
        d.set_io$$rxScalePresent(0);d.set_io$$rxScale(0);
        d.set_io$$payloadStored(1);d.set_io$$payloadToken(1);d.set_io$$rxSackPermitted(1);
        blocks(0,0);
        d.set_reset(1);d.step();d.step();d.set_reset(0);step();
    }
    void step(){
        d.step();++cycles;
        check(!d.get_io$$invariantFailure(),"SACK enabled across generation/cache mismatch");
        check(!d.get_io$$referenceFailure(),"cached data SACK differs from original generation-guard expression");
        gaps+=d.get_io$$generationGap();enabled+=d.get_io$$sackEnabled();
        options+=d.get_io$$dataOptionLength()!=0;ackGaps+=d.get_io$$ackGap();countGaps+=d.get_io$$countGap();
    }
    void wait(unsigned n=4){while(n--)step();}
    void state(unsigned expected){wait();check(d.get_io$$state()==expected,"unexpected TCP state");}
    void blocks(uint32_t base,unsigned count){
        d.set_io$$sackCount(count);
        d.set_io$$sackLeft0(base+64);d.set_io$$sackRight0(base+96);
        d.set_io$$sackLeft1(base+128);d.set_io$$sackRight1(base+160);
        d.set_io$$sackLeft2(base+192);d.set_io$$sackRight2(base+224);
        d.set_io$$sackLeft3(base+256);d.set_io$$sackRight3(base+288);
    }
    void input(uint32_t seq,uint32_t ack,unsigned flags,unsigned length=0,bool permitted=true){
        d.set_io$$rxSequence(seq);d.set_io$$rxAcknowledgement(ack);
        d.set_io$$rxFlags(flags);d.set_io$$rxLength(length);d.set_io$$rxSackPermitted(permitted);
    }
    void rx(uint32_t seq,uint32_t ack,unsigned flags,unsigned length=0,bool permitted=true,bool drain=true){
        input(seq,ack,flags,length,permitted);d.set_io$$rxValid(1);
        unsigned guard=0;do{step();check(++guard<128,"RX stalled");}while(!d.get_io$$rxReady());
        d.set_io$$rxValid(0);if(drain)wait();
    }
    void open(bool passive,bool invalid=false,bool concurrentRx=false){
        check(d.get_io$$state()==0,"OPEN outside CLOSED");
        sequence+=1000;
        d.set_io$$passive(passive);d.set_io$$initialSequence(sequence);d.set_io$$passiveInitialSequence(sequence);
        d.set_io$$remoteIp(passive?0:0x0a000002);d.set_io$$remotePort(passive?0:5000);
        d.set_io$$localPort(invalid?0:4000);
        input(0xffffffff,0,2,0,true);d.set_io$$rxValid(concurrentRx);
        auto old=d.get_io$$generation();
        d.set_io$$openValid(1);unsigned guard=0;
        do{step();check(++guard<128,"OPEN stalled");}while(!d.get_io$$openReady());
        if(concurrentRx)check(!d.get_io$$rxReady(),"OPEN did not exclude simultaneous RX");
        d.set_io$$openValid(0);d.set_io$$rxValid(0);state(invalid?0:passive?1:2);
        check(d.get_io$$generation()==old+1,"OPEN generation");
        check(!d.get_io$$sackEnabled(),"OPEN retained SACK negotiation");++opens;
    }
    void complete(){
        d.set_io$$completedGeneration(d.get_io$$generation());
        d.set_io$$completedEnd(sequence+1);d.set_io$$completedRetransmission(0);d.set_io$$completedValid(1);
        step();check(d.get_io$$completionAccepted(),"SYN completion rejected");d.set_io$$completedValid(0);wait();
    }
    void abort(){
        d.set_io$$abort(1);d.set_io$$closeValid(1);unsigned guard=0;
        do{step();check(++guard<128,"ABORT stalled");}while(!d.get_io$$closeReady());
        d.set_io$$closeValid(0);d.set_io$$abort(0);state(0);
    }
    void verifyBlocks(uint32_t base,bool negotiated){
        for(unsigned count: {1U,4U,2U,0U,3U}){
            blocks(base,count);step();wait(2);
            check(d.get_io$$dataOptionLength()==(negotiated&&count?4+8*count:0),"stable SACK option length");
        }
        // ACK-base change invalidates one cached encoding cycle even with
        // unchanged nonzero count. Preserve blocks so the new snapshot survives.
        rx(base,sequence+1,16,1,negotiated,false);wait(4);
        check(d.get_io$$receiveNext()==base+1,"RX did not advance ACK base");
        check(d.get_io$$dataOptionLength()==(negotiated?28:0),"ACK refresh lost SACK option");
    }
    void passiveRound(unsigned round){
        auto peer=round&1?0xfffffff0U:10000U+round*1024;
        open(true,false,true);
        blocks(peer+1,4);rx(peer,0,2,0,true);state(3);
        check(d.get_io$$sackEnabled(),"passive SYN failed to enable SACK");
        check(d.get_io$$dataOptionLength()==36,"SACK cache did not fill before rollback");
        auto previous=d.get_io$$generation();
        rx(peer+1,0,4,0,true,false);
        // Present the next SYN immediately. The rollback cycle must suppress
        // stale SACK even if a new handshake is waiting and output is backpressured.
        d.set_io$$noticeReady(0);d.set_io$$dispositionReady(0);
        input(peer+4096,0,2,0,round%3!=0);d.set_io$$rxValid(1);
        wait(3);
        check(d.get_io$$state()==1&&d.get_io$$generation()==previous+1,"passive reset rollback generation");
        check(!d.get_io$$sackEnabled()&&d.get_io$$dataOptionLength()==0,"rollback exposed old data SACK");
        ++rollbacks;
        d.set_io$$noticeReady(1);d.set_io$$dispositionReady(1);
        unsigned guard=0;do{step();check(++guard<128,"renegotiation stalled");}while(!d.get_io$$rxReady());
        d.set_io$$rxValid(0);state(3);
        check(bool(d.get_io$$sackEnabled())==(round%3!=0),"renegotiation did not replace SACK policy");
        complete();rx(peer+4097,sequence+1,16,0,round%3!=0);state(4);
        verifyBlocks(peer+4097,round%3!=0);
        abort();
    }
    void activeRound(unsigned round){
        auto peer=round&1?0xffffffffU:500000U+round*1024;
        open(false,false,true);complete();
        bool permit=round%3!=0;blocks(peer+1,4);
        if(round&2){
            // Simultaneous open: SYN_SENT -> SYN_RECEIVED -> ESTABLISHED.
            rx(peer,0,2,0,permit);state(3);
            rx(peer,sequence+1,18,0,permit);state(4);
        }else{
            rx(peer,sequence+1,18,0,permit);state(4);
        }
        check(bool(d.get_io$$sackEnabled())==permit,"active SACK negotiation");
        verifyBlocks(peer+1,permit);
        // Exact RST closes without incrementing generation; next OPEN must
        // clear the still-negotiated flag on the same edge as its increment.
        auto old=d.get_io$$generation();
        rx(d.get_io$$receiveNext(),0,4);state(0);
        check(d.get_io$$generation()==old,"active RST unexpectedly changes generation");
        if(round%8==0)open(false,true,true);
    }
};
int main(){
    try{
        Probe p;
        for(unsigned i=0;i<64;++i){p.passiveRound(i);p.activeRound(i);}
        check(p.gaps==p.opens+p.rollbacks,"did not observe every generation-transition cache gap");
        check(p.enabled>1000&&p.options>1000&&p.ackGaps>=128&&p.countGaps>=128,"insufficient SACK compatibility coverage");
        std::cout<<"PASS tcp_generation cycles="<<p.cycles<<" opens="<<p.opens
            <<" rollbacks="<<p.rollbacks<<" generation_gaps="<<p.gaps<<" enabled_cycles="<<p.enabled
            <<" option_cycles="<<p.options<<" ack_gaps="<<p.ackGaps<<" count_gaps="<<p.countGaps
            <<" cases=active/passive/simultaneous/invalid-open/overlapping-rx/rollback/renegotiate/wrap/backpressure"
            <<" oracle=original-generation-guard-every-cycle\n";
    }catch(const std::exception&e){std::cerr<<"FAIL tcp_generation "<<e.what()<<'\n';return 1;}
}
