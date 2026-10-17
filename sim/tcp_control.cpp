#include "TcpControlHarness.h"
#include "tcp_control_peer.h"
using Test=TcpControlPeer<STcpControlHarness>;
int main(){try{
    Test t;
    // Active open rejects ACKs for a SYN which is queued but not yet transmitted.
    t.d.set_io$$receiveWindow(200000);t.open(false,0xfffffff0U,true,2);Packet original=t.packets.back();
    check(original.window==65535,"unscaled SYN advertised window");
    Segment s;s.seq=0xfffffff0U;s.ack=0xfffffff1U;s.flags=18;s.scalePresent=true;s.scale=3;s.mssPresent=true;s.mss=1200;s.window=2000;
    t.rx(s);t.expectState(SynSent);check(t.packets.back().flags==4&&t.d.get_io$$sendOldest()==0xfffffff0U,"premature SYN ACK accepted");
    t.finish(original);t.rx(s);t.expectState(Established);
    check(t.d.get_io$$sendWindow()==2000&&t.d.get_io$$peerScale()==3&&t.packets.back().window==50000,"WS SYN negotiation");
    check(t.acks.back().handshake&&t.notices.back().flags==1,"established notification");
    t.reserve(1000);uint32_t end=t.d.get_io$$sendNext(),next=t.d.get_io$$receiveNext();
    s=Segment{};s.seq=next;s.ack=end;s.length=5;
    t.rx(s);check(t.decisions.back().length==0&&t.d.get_io$$sendOldest()==0xfffffff1U&&t.d.get_io$$receiveNext()==next,"ACK beyond actual wire send");
    t.finish(end+1,false,{},false,true);t.finish(end);
    s.length=0;s.window=3000;t.rx(s);check(t.d.get_io$$sendWindow()==24000&&t.d.get_io$$sendOldest()==end,"scaled window / ACK advance");
    s.length=100;s.stored=false;t.rx(s);check(!t.decisions.back().keep&&t.packets.back().ack==next,"unstored data acknowledged");
    s.stored=true;s.seq=next-20;s.length=80;s.flags=48;s.urgent=25;t.rx(s);
    check(t.decisions.back().skip==20&&t.decisions.back().length==60&&t.decisions.back().urgent&&
        t.decisions.back().urgentSequence==next+5&&t.d.get_io$$receiveNext()==next+60,"overlap trim / urgent indication / sequence wrap");
    next+=60;s.flags=16;s.urgent=0;s.seq=next+50;s.length=20;t.rx(s);
    check(!t.decisions.back().keep&&t.packets.back().ack==next,"out-of-order data incorrectly acknowledged");
    t.d.set_io$$receiveWindow(32);s.seq=next;s.length=50;t.rx(s);
    check(t.decisions.back().length==32&&t.d.get_io$$receiveNext()==next+32&&t.packets.back().window==0,"right-edge clipping");
    next+=32;t.d.set_io$$receiveWindow(0);s.seq=next;s.length=1;t.rx(s);
    check(!t.decisions.back().keep&&t.d.get_io$$receiveNext()==next,"zero-window payload accepted");
    s.length=0;t.rx(s);check(t.acks.back().number==end,"zero-window ACK rejected");
    s.flags=4;s.length=1;t.rx(s);t.expectState(Closed);check(t.notices.back().flags==12,"exact RST at zero window");

    // Passive SYN data stays hidden from applications until the third ACK.
    t.d.set_io$$receiveWindow(4096);t.open(true,100,true,4);s=Segment{};s.seq=1000;s.flags=2;s.length=10;s.scalePresent=true;s.scale=2;
    t.rx(s);t.expectState(SynReceived);Packet synack=t.packets.back();
    check(t.decisions.back().length==10&&t.decisions.back().early&&synack.ack==1011,"passive SYN data retention");
    t.reserve(1,false);t.finish(synack);t.rx(s);check(t.packets.back().flags==18&&t.packets.back().retransmission&&!t.decisions.back().keep,"duplicate SYN replay");
    s.seq=1011;s.flags=16;s.ack=100;s.length=0;t.rx(s);t.expectState(SynReceived);check(t.packets.back().flags==4,"bad third ACK");
    s.ack=101;s.length=8;s.window=300;t.rx(s);t.expectState(Established);
    check(t.d.get_io$$sendWindow()==1200&&t.decisions.back().length==8&&!t.decisions.back().early&&t.d.get_io$$receiveNext()==1019,"passive third ACK data");
    check(t.d.get_io$$sendMss()==536,"default IPv4 peer MSS");
    t.abort();

    // A reset returns a passive half-open TCB to its listener with a new generation.
    t.open(true,500);s=Segment{};s.seq=700;s.flags=2;t.rx(s);synack=t.packets.back();t.finish(synack);
    s.seq=701;s.flags=4;t.rx(s);t.expectState(Listen);uint32_t oldEpoch=t.epoch;t.epoch=t.d.get_io$$generation();
    t.finish(501,true,oldEpoch,false);t.d.set_io$$passiveInitialSequence(900);
    s.seq=1700;s.flags=2;t.rx(s);check(t.packets.back().seq==900,"new passive initial sequence");
    t.finish(t.packets.back());s.seq=1701;s.ack=901;s.flags=16;t.rx(s);t.expectState(Established);t.abort();

    // Simultaneous open receives the same SYN data twice but only retains it once.
    t.open(false,200,true,2);t.finish(t.packets.back());s=Segment{};s.seq=300;s.flags=2;s.length=7;s.scalePresent=true;s.scale=3;
    t.rx(s);t.expectState(SynReceived);check(t.decisions.back().early&&t.decisions.back().length==7,"simultaneous-open SYN data");
    t.finish(t.packets.back());s.flags=18;s.ack=201;s.window=100;t.rx(s);t.expectState(Established);
    check(!t.decisions.back().keep&&t.d.get_io$$receiveNext()==308&&t.d.get_io$$sendWindow()==100,"simultaneous-open SYN ACK window/data");
    t.abort();

    // FIN arriving on SYN is retained but notification waits for establishment.
    t.open(true,800);s=Segment{};s.seq=900;s.flags=3;s.length=5;t.rx(s);t.expectState(SynReceived);
    check(t.decisions.back().fin&&t.decisions.back().early&&t.d.get_io$$receiveNext()==907,"deferred SYN FIN");
    t.finish(t.packets.back());s.seq=907;s.ack=801;s.flags=16;s.length=0;t.rx(s);t.expectState(CloseWait);
    check(t.notices.back().flags==3,"deferred FIN notification");t.close();t.expectState(LastAck);
    Packet lastfin=t.packets.back();t.finish(lastfin);s.ack=lastfin.seq+1;t.rx(s);t.expectState(Closed);

    // ACK/window ordering, congestion/MSS gating, and retransmission requests.
    t.active(1000,2000);t.reserve(1201,false);t.d.set_io$$congestionWindow(10);t.reserve(11,false);t.reserve(10);t.d.set_io$$congestionWindow(65535);
    end=t.d.get_io$$sendNext();t.finish(end);size_t rp=t.replays.size();t.retry();check(t.replays.size()==rp+1&&t.replays.back()==1001,"data replay oldest");
    s=Segment{};s.seq=2002;s.ack=1001;s.window=4000;t.rx(s);check(t.d.get_io$$sendWindow()==4000,"new window update");
    s.seq=2001;s.window=1;t.rx(s);check(t.d.get_io$$sendWindow()==4000,"old segment shrank send window");
    s.seq=2002;s.window=4000;t.rx(s);check(t.acks.back().duplicate,"duplicate ACK event");
    s.seq=2001;s.ack=end;t.rx(s);check(t.d.get_io$$sendOldest()==end,"progress ACK");
    rp=t.replays.size();t.retry();check(t.replays.size()==rp,"empty-flight replay");
    s.ack=end-6000;size_t pc=t.packets.size();t.rx(s);check(t.packets.size()==pc+1&&t.packets.back().flags==16,"too-old ACK challenge");
    pc=t.packets.size();t.rx(s);check(t.packets.size()==pc,"challenge ACK throttling");
    t.ticks(4);s=Segment{};s.seq=2002;s.flags=4;pc=t.packets.size();t.rx(s);t.expectState(Established);
    check(t.packets.size()==pc+1&&t.packets.back().flags==16,"in-window nonexact RST challenge");
    s.seq=90000;pc=t.packets.size();t.rx(s);check(t.packets.size()==pc,"out-of-window RST response");
    s.seq=t.d.get_io$$receiveNext();t.rx(s);t.expectState(Closed);

    // SYN retries are generated only after actual transmission, and are tagged.
    t.open(false,3000);pc=t.packets.size();t.retry();check(t.packets.size()==pc,"unsent SYN retransmitted");
    t.finish(t.packets.back());t.retry();check(t.packets.back().retransmission&&t.packets.back().seq==3000,"SYN retransmission");
    pc=t.packets.size();t.retry(t.epoch-1);check(t.packets.size()==pc,"old generation retry");
    s=Segment{};s.seq=4000;s.ack=3001;s.flags=18;t.rx(s);t.expectState(Established);
    // Closing waits for reserved data to reach the wire, but RX remains available.
    t.reserve(20);end=t.d.get_io$$sendNext();t.d.set_io$$closeValid(1);t.d.set_io$$abort(0);t.step();
    check(!t.d.get_io$$closeReady()&&t.d.get_io$$rxReady(),"close blocked RX while waiting for queued data");
    t.d.set_io$$completedGeneration(t.epoch);t.d.set_io$$completedEnd(end);t.d.set_io$$completedRetransmission(0);t.d.set_io$$completedValid(1);
    t.step();check(t.d.get_io$$closeReady(),"close did not see simultaneous wire completion");
    t.d.set_io$$completedValid(0);t.d.set_io$$closeValid(0);t.drain();t.expectState(FinWait1);
    lastfin=t.packets.back();check(lastfin.seq==end,"FIN sequence after queued data");t.finish(lastfin);
    s=Segment{};s.seq=4001;s.ack=end;s.flags=16;t.rx(s);t.retry();
    check(t.packets.back().flags==17&&t.packets.back().seq==end&&t.packets.back().retransmission,"FIN replay");
    s.flags=17;t.rx(s);t.expectState(Closing);s.seq=4002;s.ack=end+1;s.flags=16;t.rx(s);t.expectState(TimeWait);
    t.ticks(12);s.seq=4001;s.flags=17;t.rx(s);t.expectState(TimeWait);check(t.packets.back().ack==4002,"duplicate FIN ACK in TIME-WAIT");
    s.seq=4002;s.flags=4;t.rx(s);t.expectState(TimeWait);t.ticks(24);t.expectState(TimeWait);t.ticks(1);t.expectState(Closed);

    // With MAX.SND.WND zero, a half-space ACK is neither newer nor older in
    // signed sequence arithmetic. It must still fail the bounded ACK interval.
    t.open(false,9000);t.finish(t.packets.back());s=Segment{};s.seq=10000;s.ack=9001;s.flags=18;s.window=0;t.rx(s);
    t.expectState(Established);t.reserve(1,false);s.flags=16;s.seq=10001;s.ack=9001U+0x80000000U;s.length=1;t.rx(s);
    check(!t.decisions.back().keep&&t.d.get_io$$receiveNext()==10001&&t.packets.back().ack==10001,"half-space ACK with zero maximum window");
    s.ack=9001;s.length=0;s.window=5000;t.rx(s);t.reserve(1);t.abort();

    // Clamp option values to representable IPv4/TCP and RFC WS limits.
    t.open(false,12000,true,15,65535);t.finish(t.packets.back());s=Segment{};
    s.seq=13000;s.ack=12001;s.flags=18;s.window=65535;s.scalePresent=true;s.scale=15;s.mssPresent=true;s.mss=65535;t.rx(s);
    check(t.d.get_io$$sendMss()==65495&&t.d.get_io$$peerScale()==14,"MSS/WS maximum clamp");
    s.flags=16;s.seq=13001;t.rx(s);check(t.d.get_io$$sendWindow()==(65535U<<14),"maximum scaled send window");
    t.reserve(65535,false);t.reserve(65495);t.abort();
    t.open(false,14000,false,0,0);t.finish(t.packets.back());s=Segment{};s.seq=15000;s.ack=14001;s.flags=18;s.mssPresent=true;s.mss=0;t.rx(s);
    check(t.d.get_io$$sendMss()==1,"zero configured MSS bounded away from zero");t.reserve(2,false);t.reserve(1);t.abort();

    // Seeded independent peer scenarios exercise all close paths, sequence wrap,
    // duplicate data, failed storage, and independently stalled output channels.
    for(unsigned run=0;run<240;++run){
        bool passive=run%2,scale=run%3;unsigned ls=run%5,ps=(run/5)%5;
        uint32_t local=run%3?uint32_t(t.rng()):0xfffffff0U,peer=run%4?uint32_t(t.rng()):0xffffffe0U;
        t.d.set_io$$receiveWindow(4096);t.d.set_io$$congestionWindow(65535);
        if(passive)t.passive(local,peer,scale,ls,ps);else t.active(local,peer,scale,ls,ps);
        uint32_t recv=peer+1,sent=local+1;
        for(unsigned round=0;round<5;++round){
            unsigned n=1+t.rng()%160;t.reserve(n);sent+=n;t.finish(sent);
            Segment p;p.seq=recv;p.ack=sent;p.length=1+t.rng()%150;p.stored=false;
            t.rx(p);check(t.d.get_io$$sendOldest()==sent&&t.d.get_io$$receiveNext()==recv&&!t.decisions.back().keep,"random unretained segment");
            p.stored=true;t.rx(p);check(t.decisions.back().length==p.length&&t.packets.back().ack==recv+p.length,"random retained segment");
            recv+=p.length;t.rx(p);check(!t.decisions.back().keep&&t.d.get_io$$receiveNext()==recv,"duplicate data retained twice");
            p.seq=recv+20;p.length=5;t.rx(p);check(!t.decisions.back().keep,"random gap accepted without reorder storage");
            p.seq=recv;p.length=20;t.rx(p);recv+=20;
            p.seq=recv;p.length=5;t.rx(p);recv+=5;
            check(t.d.get_io$$receiveNext()==recv,"gap recovery");
        }
        Segment p;p.seq=recv;p.ack=sent;p.flags=17;
        if(run%3==0){
            t.rx(p);++recv;t.expectState(CloseWait);t.reserve(10);sent+=10;t.finish(sent);
            p.seq=recv;p.ack=sent;p.flags=16;t.rx(p);
            t.close();t.expectState(LastAck);lastfin=t.packets.back();t.finish(lastfin);
            p.ack=sent+1;t.rx(p);t.expectState(Closed);
        }else{
            t.close();t.expectState(FinWait1);lastfin=t.packets.back();t.finish(lastfin);
            if(run%3==1){p.flags=16;p.ack=sent+1;t.rx(p);t.expectState(FinWait2);p.flags=17;t.rx(p);}
            else{p.ack=sent+1;t.rx(p);}
            t.expectState(TimeWait);t.ticks(25);t.expectState(Closed);
        }
        uint32_t old=t.epoch;
        if(run%40==0){
            t.open(false,local+10000);uint32_t before=t.d.get_io$$sendActual();
            t.finish(local+1,false,old,false);check(t.d.get_io$$sendActual()==before,"old connection completion corrupted new generation");t.close();t.expectState(Closed);
        }
    }
    check(t.states==2047&&t.receives>7000&&t.reservations>1200,"TCP control coverage");
    std::cout<<"TCP_CONNECTION_CONTROL_PASS cycles="<<t.cycles<<" segments="<<t.receives<<" reservations="<<t.reservations
        <<" control_packets="<<t.packets.size()<<" dispositions="<<t.decisions.size()<<" ack_events="<<t.acks.size()
        <<" notices="<<t.notices.size()<<" states=11 handshake=pass half_close=pass time_wait=pass"
        <<" simultaneous_open=pass durable_ack=pass stale_generation=pass\n";
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
