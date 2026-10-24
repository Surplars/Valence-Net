#include "TcpControlHarness.h"
#include "tcp_control_peer.h"
#include "gsim_model_init.h"
struct ScaleModel : STcpControlHarness {
    ScaleModel(){ initializeGsimModel(*this,&STcpControlHarness::init); }
};
using Test=TcpControlPeer<ScaleModel>;
static void window(Test& t,uint32_t credit,unsigned shift){
    t.d.set_io$$receiveWindow(credit);t.step();
    auto expected=std::min(credit>>shift,65535U);
    check(t.d.get_io$$advertisedWindow()==expected,"negotiated advertised window differs from independent shift/saturation");
}
int main(){try{
    unsigned cases=0;
    for(unsigned requested=0;requested<16;++requested){
        Test t;unsigned shift=std::min(requested,14U);uint32_t quantum=1U<<shift;
        t.d.set_io$$receiveWindow(200003);t.open(true,1000,true,requested);
        check(!t.d.get_io$$scaleEnabled(),"OPEN did not clear negotiated scale");
        window(t,200003,0);
        Segment s;s.flags=2;s.seq=0xffffffe0U;s.scalePresent=true;s.scale=3;
        t.rx(s);t.expectState(SynReceived);check(t.d.get_io$$scaleEnabled(),"passive scale negotiation");
        auto synack=t.packets.back();check(synack.window==65535,"passive SYN window was scaled");
        std::vector<uint32_t> credits{0,1,15,65534,65535,65536,65537,0x7fffffffU,0xffffffffU,
            quantum-1,quantum,quantum+1,65535U<<shift,(65535U<<shift)+quantum-1};
        for(auto credit:credits){window(t,credit,shift);++cases;}
        t.finish(synack);
        s=Segment{};s.flags=4;s.seq=t.d.get_io$$receiveNext();t.rx(s);t.expectState(Listen);
        t.epoch=t.d.get_io$$generation();
        check(!t.d.get_io$$scaleEnabled(),"passive reset did not clear scaleEnabled");
        window(t,65537,0); // Fails if one-hot decode survives passive rollback.
        s=Segment{};s.flags=2;s.seq=2000;s.scalePresent=false;
        t.rx(s);t.expectState(SynReceived);check(!t.d.get_io$$scaleEnabled(),"fallback renegotiation retained scaling");
        window(t,200003,0);t.finish(t.packets.back());
        s=Segment{};s.seq=2001;s.ack=1001;t.rx(s);t.expectState(Established);
        window(t,65537,0);t.abort();
        // The active third ACK uses this cycle's negotiation, not the old register.
        t.d.set_io$$receiveWindow(200003);t.active(3000,4000,true,requested,2);
        check(t.packets.back().window==std::min(200003U>>shift,65535U),"active negotiation ACK used stale scale");
        for(auto credit:credits){window(t,credit,shift);++cases;}
        t.d.set_io$$receiveWindow(0);t.d.set_io$$updateWindow(1);t.step();t.d.set_io$$updateWindow(0);t.drain();
        check(t.packets.back().window==0,"zero window advertisement");
        t.d.set_io$$receiveWindow(3*quantum+quantum/2);t.d.set_io$$updateWindow(1);t.step();t.d.set_io$$updateWindow(0);t.drain();
        check(t.packets.back().window==3,"scaled reopen advertisement");
        t.abort();
    }
    std::cout<<"WINDOW_SCALE_PASS requested_scales=16 credit_cases="<<cases
        <<" passive_rollback=pass renegotiation_fallback=pass active_same_cycle=pass saturation=pass reopen=pass\n";
    return 0;
}catch(const std::exception&e){std::cerr<<"window_scale: "<<e.what()<<'\n';return 1;}}
