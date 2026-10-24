#define main pmtu_existing_main
#include "tcp_pmtu.cpp"
#undef main

static void rawPath(Test& t,unsigned mtu){
 t.d.set_io$$pathUpdateMtu(mtu);t.d.set_io$$pathUpdateIncrease(0);
 t.d.set_io$$pathUpdateGeneration(t.d.get_io$$generation());
 t.d.set_io$$pathUpdateValid(1);t.step();t.d.set_io$$pathUpdateValid(0);
}
static void finishRepair(Test& t,size_t cursor,uint32_t acked){
 unsigned limit=3000;
 while(acked!=t.planned&&limit--){
  if(cursor==t.packets.size()){t.step();continue;}
  Packet p=t.packets[cursor++];
  if(!p.meta.data||!p.meta.retry)continue;
  check(p.bytes.size()<=600,"staged repair exceeded current PMTU");
  uint32_t delta=p.meta.end-acked;
  if(delta&&!(delta>>31)){acked=p.meta.end;t.ack(acked);}
 }
 check(acked==t.planned,"stale PMTU offer abandoned remaining repair horizon");
 t.drain();t.idle(60);
 check(t.d.get_io$$freeBanks()==4,"staged PMTU lookup leaked retained bank");
 check(t.d.get_io$$sendNext()==t.planned,"staged PMTU lookup allocated sequence space");
}
int main(){try{
 unsigned partial=0,full=0,aborts=0,passive=0;
 for(unsigned delay=0;delay<14;++delay){
  Test t;t.random=false;t.active(0xffffff00U+delay*0x1000U,2000+delay*100);
  uint32_t start=t.planned;t.write(1400);t.until([&]{return t.d.get_io$$sendActual()==t.planned;});t.drain();
  size_t cursor=t.packets.size();rawPath(t,600);t.idle(delay);t.ack(start+137);
  finishRepair(t,cursor,start+137);check(t.d.get_io$$retries()==0,"ACK race charged RTO retry");++partial;
 }
 for(unsigned delay=0;delay<10;++delay){
  Test t;t.random=false;t.active(100000+delay*10000,5000+delay*100);
  t.write(1400);t.until([&]{return t.d.get_io$$sendActual()==t.planned;});t.drain();
  rawPath(t,600);t.idle(delay);t.ack(t.planned);t.drain();t.idle(100);
  check(t.d.get_io$$freeBanks()==4&&t.d.get_io$$sendOldest()==t.planned,"full ACK did not cancel staged repair");
  check(t.d.get_io$$retries()==0,"full ACK race charged RTO");++full;
 }
 for(unsigned delay=0;delay<10;++delay){
  Test t;t.random=false;t.active(300000+delay*10000,8000+delay*100);
  t.write(1400);t.until([&]{return t.d.get_io$$sendActual()==t.planned;});t.drain();
  auto old=t.d.get_io$$generation();rawPath(t,600);t.idle(delay);t.close(true);t.drain();
  check(t.d.get_io$$state()==0,"abort during staged repair did not close");
  t.active(500000+delay*10000,10000+delay*100);check(t.d.get_io$$generation()==old+1,"abort/reopen epoch");
  unsigned retries=t.replayed;t.path(600,false,-1);t.write(333);
  t.until([&]{return t.d.get_io$$sendActual()==t.planned;});t.ack(t.planned);t.drain();t.idle(60);
  check(t.d.get_io$$pathMtu()==1500&&t.replayed==retries,"old staged offer crossed OPEN generation");
  check(t.d.get_io$$freeBanks()==4,"reopen lookup leaked bank");++aborts;
 }
 for(unsigned delay=0;delay<6;++delay){
  Test t;t.random=false;t.open(700000+delay*10000,true);t.peerNext=12001+delay*100;
  t.rx(t.peerNext-1,0,2);t.until([&]{return t.d.get_io$$sendActual()==t.planned;});t.drain();
  check(t.d.get_io$$state()==3,"passive first SYN did not reach SYN-RECEIVED");
  auto old=t.d.get_io$$generation();rawPath(t,600);t.idle(delay);t.rx(t.peerNext,0,4);t.drain();
  check(t.d.get_io$$state()==1&&t.d.get_io$$generation()==old+1,"passive RST did not cancel old generation");
  check(t.d.get_io$$pathMtu()==1500,"passive RST retained learned path");
  t.peerNext+=1000;t.rx(t.peerNext-1,0,2);t.until([&]{return t.d.get_io$$sendActual()==t.planned;});
  t.rx(t.peerNext,t.planned,16);check(t.d.get_io$$state()==4,"second passive handshake failed");
  t.drain();unsigned retries=t.replayed;t.write(333);t.until([&]{return t.d.get_io$$sendActual()==t.planned;});
  t.ack(t.planned);t.drain();check(t.replayed==retries&&t.d.get_io$$freeBanks()==4,"passive RST staged offer survived");++passive;
 }
 {
  Test t;t.random=false;t.active(1000000,20000);t.write(1400);
  t.until([&]{return t.d.get_io$$sendActual()==t.planned;});t.drain();t.stallOutput=true;
  size_t cursor=t.packets.size();unsigned ticks=0;
  while(!t.d.get_io$$timerExpired()&&ticks++<3000)t.step(true);
  check(t.d.get_io$$timerExpired(),"RTO collision setup did not expire");
  rawPath(t,600);t.idle(30);t.stallOutput=false;
  t.until([&]{return t.packets.size()>cursor;});
  check(t.packets[cursor].meta.timeoutRetry,"staged PMTU offer stole loss replay priority");
  // An RTO prepared before the decrease can remain at its original size.
  Packet first=t.packets[cursor++];uint32_t acked=first.meta.end;t.ack(acked);
  finishRepair(t,cursor,acked);
 }
 std::cout<<"TCP_PMTU_PIPELINE_PASS partial_ack_offsets="<<partial<<" full_ack_offsets="<<full
          <<" abort_reopen_offsets="<<aborts<<" passive_rst_offsets="<<passive<<" loss_priority=pass\n";
 return 0;
}catch(const std::exception&e){std::cerr<<"TCP_PMTU_PIPELINE_FAIL "<<e.what()<<'\n';return 1;}}
