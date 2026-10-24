#include "TcpControlHarness.h"
#include "tcp_control_peer.h"
#include "gsim_model_init.h"
struct BodyModel : STcpControlHarness {
    BodyModel(){initializeGsimModel(*this,&STcpControlHarness::init);}
};
using Test=TcpControlPeer<BodyModel>;
int main(){try{
    unsigned cases=0,finCases=0,clipped=0,failedStores=0;
    for(unsigned requested=0;requested<16;++requested){
        unsigned shift=std::min(requested,14U);uint32_t quantum=1U<<shift;
        std::vector<uint32_t> credits{0,quantum-1,quantum,quantum+quantum/2,3*quantum+quantum/2,0xffffffffU};
        for(auto credit:credits){
            uint32_t effective=std::min(credit>>shift,65535U)<<shift;
            std::vector<uint32_t> choices{effective?effective-1:0,effective,effective+1};
            for(auto choice:choices)for(unsigned overlap:{0U,7U})for(bool stored:{false,true}){
                unsigned available=std::min(choice,65535U-overlap);
                Test t;t.d.set_io$$receiveWindow(credit);t.active(3000,0xffffffe0U,true,requested,3);
                uint32_t base=t.d.get_io$$receiveNext();
                Segment s;s.seq=base-overlap;s.ack=t.d.get_io$$sendActual();s.flags=17;
                s.length=overlap+available;s.stored=stored;
                t.rx(s);
                // Preserve the endpoint's existing first/last sequence-window
                // admission before assessing its contiguous body arithmetic.
                bool sequenceAccepted=effective!=0&&
                    (uint32_t(s.seq-base)<effective||uint32_t(s.seq+s.length-base)<effective);
                unsigned kept=sequenceAccepted&&stored?std::min(available,effective):0;
                bool takeFin=sequenceAccepted&&available<effective&&(stored||available==0);
                check(t.decisions.back().length==kept&&t.decisions.back().keep==bool(kept),
                    "receive payload clip differs from independent window/overlap calculation");
                check(t.decisions.back().fin==takeFin,"FIN accepted at or beyond right edge / missing within window");
                check(t.d.get_io$$receiveNext()==uint32_t(base+kept+takeFin),"wrapped receive right edge changed");
                check(t.packets.back().ack==uint32_t(base+kept+takeFin),"control ACK mismatches durable bytes and FIN");
                t.expectState(takeFin?CloseWait:Established);
                ++cases;finCases+=takeFin;clipped+=stored&&available>effective;failedStores+=!stored;
            }
        }
    }
    std::cout<<"BODY_WINDOW_PASS cases="<<cases<<" accepted_fin="<<finCases<<" clipped="<<clipped
        <<" failed_storage="<<failedStores<<" scales=16 boundary_equality=pass overlap=pass wrap=pass\n";return 0;
}catch(const std::exception&e){std::cerr<<"body_window: "<<e.what()<<'\n';return 1;}}
