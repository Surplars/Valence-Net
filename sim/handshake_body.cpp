#include "TcpControlHarness.h"
#include "tcp_control_peer.h"
#include "gsim_model_init.h"
struct BodyModel : STcpControlHarness {
    BodyModel(){initializeGsimModel(*this,&STcpControlHarness::init);}
};
using Test=TcpControlPeer<BodyModel>;
int main(){try{
    unsigned cases=0;
    for(bool passive:{false,true})for(bool stored:{false,true})for(bool fin:{false,true})
    for(uint32_t seq:{0U,0xfffffffeU,0xffffffffU})
    for(uint32_t credit:{0U,1U,7U,8U,65534U,65535U,65536U,0xffffffffU})
    for(unsigned length:{0U,1U,6U,7U,8U,65533U,65534U,65535U}){
        Test t;t.d.set_io$$receiveWindow(credit);
        t.open(passive,0xfffffff0U,true,2);
        if(!passive)t.finish(t.packets.back());
        Segment s;s.seq=seq;s.ack=passive?0U:0xfffffff1U;
        s.flags=(passive?2U:18U)|unsigned(fin);s.length=length;s.stored=stored;
        s.scalePresent=true;s.scale=3;
        t.rx(s);
        unsigned window=std::min(credit,65535U);
        unsigned kept=stored?std::min(length,window):0;
        bool takeFin=fin&&length<window&&(stored||length==0);
        uint32_t next=seq+1+kept+unsigned(takeFin);
        auto d=t.decisions.back();
        check(d.skip==0&&d.length==kept&&d.keep==bool(kept),"SYN payload clip");
        check(d.fin==takeFin&&d.early==passive,"SYN FIN/establishment ownership");
        check(t.d.get_io$$receiveNext()==next&&t.packets.back().ack==next,"SYN wrapped ACK edge");
        if(passive)check(t.packets.back().window==window-kept,"SYN window must remain unscaled");
        t.expectState(passive?SynReceived:(takeFin?CloseWait:Established));
        ++cases;
    }
    std::cout<<"HANDSHAKE_BODY_PASS cases="<<cases
        <<" active/passive=pass storage=pass clip/equality=pass fin=pass wrap=pass unscaled_window=pass\n";
    return 0;
}catch(const std::exception&e){std::cerr<<"handshake_body: "<<e.what()<<'\n';return 1;}}
