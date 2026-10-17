#include "TcpManagedHarness.h"
#include "tcp_control_peer.h"
using Test=TcpControlPeer<STcpManagedHarness>;
int main(){try{
    Test t;
    t.open(false,0xfffffff0U);Packet syn=t.packets.back();size_t pc=t.packets.size();
    t.ticks(100);check(t.packets.size()==pc&&!t.d.get_io$$timerRunning(),"queued SYN started retransmission timer");
    t.finish(syn);check(t.d.get_io$$timerRunning()&&t.d.get_io$$rto()==10,"SYN wire completion did not arm timer");
    t.ticks(10);check(t.packets.size()==pc,"SYN retransmitted before full RTO");
    t.ticks(1);check(t.packets.size()==pc+1&&t.packets.back().retransmission&&t.packets.back().seq==syn.seq,"automatic SYN replay");
    Packet retry=t.packets.back();t.ticks(100);
    check(t.packets.size()==pc+1&&t.d.get_io$$rto()==20&&t.d.get_io$$retries()==0,"queued retry counted or backed off repeatedly");
    t.finish(retry);check(t.d.get_io$$retries()==1&&t.d.get_io$$timerRunning(),"completed retry accounting");
    t.ticks(3);Segment s;s.seq=1000;s.ack=syn.seq+1;s.flags=18;t.rx(s);t.expectState(Established);
    check(t.d.get_io$$rto()==30&&!t.d.get_io$$timerRunning()&&t.d.get_io$$retries()==0,"SYN fallback RTO / handshake ACK");
    t.reserve(100);uint32_t end=t.d.get_io$$sendNext();t.finish(end);t.ticks(2);
    s=Segment{};s.seq=1001;s.ack=end;t.rx(s);
    check(t.d.get_io$$rto()==10&&!t.d.get_io$$timerRunning(),"fresh data RTT did not replace backed-off RTO");

    t.reserve(100);end=t.d.get_io$$sendNext();uint32_t previous=t.d.get_io$$sendOldest();t.finish(end);
    size_t rc=t.replays.size();t.ticks(11);
    check(t.replays.size()==rc+1&&t.replays.back()==previous,"automatic data replay request");
    t.ticks(50);check(t.replays.size()==rc+1&&t.d.get_io$$retries()==0,"data scheduler backpressure repeated replay");
    t.finish(end,true);check(t.d.get_io$$retries()==1,"data retransmission completion");
    s.ack=previous+10;t.rx(s);check(t.d.get_io$$retries()==0&&t.d.get_io$$timerRunning(),"partial ACK did not restart/reset retry count");
    t.ticks(t.d.get_io$$rto()+1);check(t.replays.back()==previous+10,"partial-ACK retransmission offset");
    s.ack=end;t.rx(s);check(!t.d.get_io$$timerRunning(),"all-data ACK left timer armed");
    // The scheduler had already accepted a replay. Its late completion is
    // harmless after the ACK has reclaimed the corresponding sequence range.
    t.finish(end,true,{},false,false);check(!t.d.get_io$$invalidCompletion()&&!t.d.get_io$$timerRunning(),"retired replay completion restarted timer");

    // ACK consumer stalls must not shift the measurement/stop point.
    t.reserve(50);end=t.d.get_io$$sendNext();t.finish(end);t.ticks(2);
    t.stallAck=true;s.ack=end;t.rx(s,false);
    for(unsigned i=0;i<50;++i)t.step(true);
    check(t.d.get_io$$ackValid()&&!t.d.get_io$$timerRunning()&&!t.d.get_io$$timerExpired(),"ACK event backpressure delayed timer update");
    unsigned stableRto=t.d.get_io$$rto();for(unsigned i=0;i<50;++i)t.step(true);
    check(t.d.get_io$$rto()==stableRto,"held ACK sampled repeatedly");
    t.stallAck=false;t.drain();
    t.reserve(50);end=t.d.get_io$$sendNext();t.finish(end);unsigned timeout=t.d.get_io$$rto();
    t.ticks(timeout);pc=t.packets.size();rc=t.replays.size();s.ack=end;t.rx(s,true,true);
    check(!t.d.get_io$$timerRunning()&&!t.d.get_io$$timerExpired()&&t.replays.size()==rc&&t.packets.size()==pc,"ACK arrival / RTO boundary created replay");
    t.abort();

    // Passive ISN can differ from the OPEN command's placeholder.
    t.open(true,123);t.d.set_io$$passiveInitialSequence(0xfffffff8U);
    s=Segment{};s.seq=2000;s.flags=2;t.rx(s);t.expectState(SynReceived);syn=t.packets.back();
    check(syn.seq==0xfffffff8U,"passive ISN setup");t.ticks(50);check(!t.d.get_io$$timerRunning(),"passive SYN-ACK armed before send");
    t.finish(syn);t.ticks(11);retry=t.packets.back();check(retry.flags==18&&retry.retransmission,"passive SYN-ACK timeout");
    t.finish(retry);s.seq=2001;s.ack=syn.seq+1;s.flags=16;t.rx(s);t.expectState(Established);
    check(!t.d.get_io$$timerRunning()&&t.d.get_io$$rto()==30,"passive handshake timer ownership");t.abort();

    // FIN uses the same automatic retry machinery; TIME-WAIT owns no RTO.
    t.active(4000,5000);t.close();t.expectState(FinWait1);Packet fin=t.packets.back();pc=t.packets.size();
    t.ticks(50);check(t.packets.size()==pc&&!t.d.get_io$$timerRunning(),"queued FIN started RTO");
    t.finish(fin);t.ticks(11);retry=t.packets.back();check(retry.flags==17&&retry.seq==fin.seq&&retry.retransmission,"automatic FIN replay");
    t.finish(retry);s=Segment{};s.seq=5001;s.ack=fin.seq+1;t.rx(s);t.expectState(FinWait2);
    check(!t.d.get_io$$timerRunning(),"FIN ACK did not stop timer");
    s.flags=17;t.rx(s);t.expectState(TimeWait);t.ticks(25);t.expectState(Closed);

    // Repeated losses eventually abort. A pending OPEN cannot block timeout
    // cleanup of the old connection; it is not accepted until CLOSED.
    t.open(false,6000);syn=t.packets.back();t.finish(syn);
    for(unsigned attempt=0;attempt<3;++attempt){
        pc=t.packets.size();t.ticks(t.d.get_io$$rto()+1);
        check(t.packets.size()==pc+1&&t.packets.back().retransmission,"bounded SYN retry");
        t.finish(t.packets.back());check(t.d.get_io$$retries()==attempt+1,"retry attempt count");
    }
    size_t ntimeouts=t.timeouts.size();t.d.set_io$$openValid(1);
    unsigned bound=t.d.get_io$$rto()+30;
    for(unsigned i=0;i<bound&&t.timeouts.size()==ntimeouts;++i)t.step(true);
    t.d.set_io$$openValid(0);t.drain();t.expectState(Closed);
    check(t.timeouts.size()==ntimeouts+1&&t.timeouts.back()==t.epoch,"timeout abort notification");
    check(t.notices.back().flags==20&&!t.d.get_io$$timerRunning()&&t.d.get_io$$rto()==10,"timeout cleanup");

    // Reuse after timeout must ignore any completion from the previous epoch.
    uint32_t old=t.epoch;t.open(false,7000);syn=t.packets.back();t.finish(6001,true,old,false);
    t.ticks(50);check(!t.d.get_io$$timerRunning(),"old-generation retry armed new timer");
    t.finish(syn);s=Segment{};s.seq=8000;s.ack=7001;s.flags=18;t.rx(s);t.expectState(Established);t.abort();

    std::cout<<"TCP_MANAGED_CONNECTION_PASS cycles="<<t.cycles<<" segments="<<t.receives
        <<" controls="<<t.packets.size()<<" data_replays="<<t.replays.size()<<" timeouts="<<t.timeouts.size()
        <<" syn_fin_retry=pass partial_ack=pass wire_timer=pass ack_stall=pass retry_bound=pass\n";
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
