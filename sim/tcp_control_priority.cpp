#include "TcpControlHarness.h"
#include "tcp_control_peer.h"
using Test=TcpControlPeer<STcpControlHarness>;
int main(){try{
 unsigned handshakes=0,reserves=0;
 for(uint32_t local:{0U,0xfffffffeU,0xffffffffU})for(bool stored:{false,true})
 for(bool fin:{false,true})for(bool completed:{false,true})for(unsigned variant=0;variant<4;++variant){
  Test t;t.open(false,local,true,2);Packet initial=t.packets.back();if(completed)t.finish(initial);
  Segment first;first.seq=0xfffffffcU;first.flags=2;first.length=7;first.stored=stored;
  first.scalePresent=true;first.scale=3;t.rx(first);t.expectState(SynReceived);
  uint32_t next=t.d.get_io$$receiveNext();
  Segment second=first;second.stored=true;second.length=11;second.flags=fin?19:18;second.ack=local+1;
  if(variant==1)++second.ack;
  if(variant==2)++second.seq;
  if(variant==3){second.seq=next;second.flags=4;second.length=0;}
  t.rx(second);
  if(variant==3){t.expectState(Closed);check(!t.decisions.back().keep,"RST reached simultaneous-open payload body");}
  else if(completed&&variant==0){
   t.expectState(fin?CloseWait:Established);
   check(t.decisions.back().skip==(stored?7U:0U)&&t.decisions.back().length==(stored?4U:11U),"simultaneous SYN ACK retained wrong overlap");
   check(t.decisions.back().fin==fin&&t.d.get_io$$receiveNext()==uint32_t(first.seq+12+unsigned(fin)),"simultaneous SYN ACK/FIN wrap edge");
   check(t.d.get_io$$sendOldest()==uint32_t(local+1),"simultaneous ACK did not retire completed SYN");
  }else{
   t.expectState(SynReceived);check(!t.decisions.back().keep&&t.d.get_io$$receiveNext()==next,"invalid/premature SYN ACK mutated payload state");
  }
  ++handshakes;
 }
 for(bool halfClosed:{false,true})for(bool queued:{false,true})for(unsigned mask=0;mask<128;++mask){
  Test t;t.active(10000,20000);
  if(halfClosed){Segment fin;fin.seq=20001;fin.ack=10001;fin.flags=17;t.rx(fin);t.expectState(CloseWait);}
  if(queued)t.reserve(3);
  bool op=mask&1,cl=mask&2,abort=mask&4,complete=mask&8,window=mask&16,rx=mask&32,retry=mask&64;
  // Install pending window work without allowing it to emit before the matrix cycle.
  if(window){t.d.set_io$$updateWindow(1);t.step();t.d.set_io$$updateWindow(0);}
  auto base=t.d.get_io$$sendNext();
  t.d.set_io$$openValid(op);t.d.set_io$$closeValid(cl);t.d.set_io$$abort(abort);
  t.d.set_io$$completedValid(complete);t.d.set_io$$completedGeneration(t.epoch-1);
  t.d.set_io$$completedEnd(base);t.d.set_io$$completedRetransmission(0);
  t.d.set_io$$rxValid(rx);t.d.set_io$$rxSourceIp(0x0a000002);t.d.set_io$$rxDestinationIp(0x0a000001);
  t.d.set_io$$rxSourcePort(5000);t.d.set_io$$rxDestinationPort(4000);
  t.d.set_io$$rxSequence(t.d.get_io$$receiveNext());t.d.set_io$$rxAcknowledgement(t.d.get_io$$sendOldest());
  t.d.set_io$$rxFlags(16);t.d.set_io$$rxLength(0);t.d.set_io$$rxErrors(0);t.d.set_io$$rxWindow(5000);
  t.d.set_io$$payloadStored(1);t.d.set_io$$payloadToken(++t.token);
  t.d.set_io$$retryValid(retry);t.d.set_io$$retryGeneration(t.epoch);
  t.d.set_io$$reserveValid(1);t.d.set_io$$reserveLength(1);
  bool closeFire=cl&&!complete&&(abort||!queued);
  bool emitWindow=window&&!op&&!cl&&!rx&&!retry&&!complete;
  bool expected=!emitWindow&&!closeFire&&!rx&&!retry;
  t.step();
  if(bool(t.d.get_io$$reserveReady())!=expected){std::cerr<<"mask="<<mask<<" half="<<halfClosed<<" queued="<<queued<<'\n';throw std::runtime_error("reserve event priority changed");}
  check(!t.d.get_io$$openReady(),"OPEN became accepted in a sending state");
  t.d.set_io$$openValid(0);t.d.set_io$$closeValid(0);t.d.set_io$$completedValid(0);
  t.d.set_io$$rxValid(0);t.d.set_io$$retryValid(0);t.d.set_io$$reserveValid(0);t.drain();
  check(t.d.get_io$$sendNext()==base+unsigned(expected)+unsigned(closeFire&&!abort),"reserve/FIN ownership changed under competing events");
  ++reserves;
 }
 std::cout<<"TCP_CONTROL_PRIORITY_PASS simultaneous_open_cases="<<handshakes<<" reserve_priority_cases="<<reserves
          <<" wrap=pass premature_ack=pass rst=pass pending_window_open=pass completion=pass half_close=pass\n";
 return 0;
}catch(const std::exception&e){std::cerr<<"TCP_CONTROL_PRIORITY_FAIL "<<e.what()<<'\n';return 1;}}
