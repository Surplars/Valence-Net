#include "TcpCongestedEndpointHarness.h"
#include "tcp_endpoint_peer.h"
using Test=TcpEndpointPeer<STcpCongestedEndpointHarness>;
int main(){try{
 Test t;t.active(1000,2000,256);check(t.d.get_io$$congestionLimit()==256,"one-SMSS initial window");
 uint32_t base=t.planned;t.write(2048);t.idle(500);
 check(t.d.get_io$$sendActual()==base+256,"initial congestion window did not constrain flight");
 t.ack(base+256);t.idle(500);
 check(t.d.get_io$$congestionLimit()==512&&t.d.get_io$$sendActual()==base+768,"slow-start growth / credit");
 t.ack(base+768);t.idle(500);
 check(t.d.get_io$$congestionLimit()==768&&t.d.get_io$$sendActual()==base+1536,"byte-counted growth");
 // Three duplicate ACKs trigger a replay without waiting for the RTO tick.
 unsigned oldReplay=t.replayed;t.ack(base+768);t.ack(base+768);t.ack(base+768);
 t.until([&]{return t.replayed>oldReplay;});t.drain();
 check(t.d.get_io$$fastRecovery()&&t.d.get_io$$congestionThreshold()==512,"fast recovery threshold");
 check(t.d.get_io$$sendActual()==t.planned,"recovery credit did not release remaining bytes");
 t.ack(t.planned);t.drain();check(!t.d.get_io$$fastRecovery()&&t.d.get_io$$congestionLimit()==512,"recovery did not deflate");
 // An RTO reduces the window to one MSS and retransmits the oldest data.
 base=t.planned;t.write(700);t.idle(300);check(t.d.get_io$$sendActual()==base+512,"post-recovery flight");
 oldReplay=t.replayed;t.idle(t.d.get_io$$rto()+1,true);t.until([&]{return t.replayed>oldReplay;});t.drain();
 check(t.d.get_io$$congestionLimit()==256,"RTO did not reduce congestion window");
 t.ack(base+512);t.until([&]{return t.d.get_io$$sendActual()==t.planned;});t.ack(t.planned);t.drain();
 // After a quiet RTO, restarting cannot burst at the old enlarged window.
 t.idle(t.d.get_io$$rto()+3,true);check(t.d.get_io$$congestionLimit()==256,"idle restart window");
 // Recover when the peer's zero-window update was lost. Probe submission
 // does not advance sequence space or consume the data-loss retry budget.
 uint32_t zeroBase=t.planned;t.rx(t.peerNext,zeroBase,16,0);t.write(256);t.idle(100);
 check(t.d.get_io$$sendActual()==zeroBase,"data sent into zero peer window");
 t.autoComplete=false;size_t beforeProbe=t.packets.size();t.idle(t.d.get_io$$rto()+2,true);
 t.until([&]{return t.packets.size()>beforeProbe;});
 auto probe=t.packets.back();check(probe.bytes[33]==16&&u32(probe.bytes,24)==zeroBase-1&&!probe.meta.consumes,"invalid zero-window probe");
 t.idle(100,true);check(t.packets.size()==beforeProbe+1&&!t.d.get_io$$timerRunning(),"queued probe repeated or armed loss timer");
 t.autoComplete=true;t.drain();beforeProbe=t.packets.size();t.idle(19,true);
 check(t.packets.size()==beforeProbe,"persist backoff fired early");
 t.idle(3,true);t.until([&]{return t.packets.size()>beforeProbe;});t.drain();
 t.rx(t.peerNext,zeroBase,16,512);t.until([&]{return t.d.get_io$$sendActual()==t.planned;});t.ack(t.planned);t.drain();
 check(t.d.get_io$$retries()==0,"zero-window probe counted as a loss retransmission");
 t.close(true);t.drain();
 std::cout<<"TCP_CONGESTED_ENDPOINT_PASS cycles="<<t.cycles<<" data_packets="<<t.dataPackets
  <<" retransmissions="<<t.replayed<<" actual_window=pass slow_start=pass duplicate_ack_replay=pass rto_loss=pass idle_restart=pass persist=pass\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
