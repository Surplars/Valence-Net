#include "EthernetServiceHarness.h"
#include "ethernet_peer.h"
using Test=EthernetPeer<SEthernetServiceHarness>;
int main(){try{
 Test t;t.active(10000,0xfffffff0);
 unsigned waves=0;
 for(unsigned round=0;round<24;++round){
  uint32_t base=t.peerNext;const unsigned n=1460,total=9*n;
  std::vector<uint8_t> bytes(total);for(auto&v:bytes)v=t.rng();
  for(auto v:bytes)t.expectedRx.push_back(v);
  std::vector<unsigned> order={1,2,3,4,5,6,7,8};std::shuffle(order.begin(),order.end(),t.rng);
  unsigned before=t.receivedBytes;t.stallReceive=true;
  for(unsigned i:order){
   t.wire(t.encode(base+i*n,t.planned,24,65535,{bytes.begin()+i*n,bytes.begin()+(i+1)*n}));
   t.idle(240);
   check(t.d.get_io$$receiveNext()==base&&t.receivedBytes==before,"full-MSS gap advanced stream");
  }
  t.peerNext+=total;
  t.wire(t.encode(base,t.planned,24,65535,{bytes.begin(),bytes.begin()+n}));
  t.until([&]{return t.d.get_io$$receiveNext()==t.peerNext;});
  t.idle(300);
  check(t.d.get_io$$receiveWindow()==65536-total,"large reassembly credit mismatch");
  t.stallReceive=false;t.until([&]{return t.expectedRx.empty();});
  t.idle(240);t.drain();
  check(u32(t.packets.back().bytes,28)==t.peerNext,"full-MSS cumulative ACK mismatch");
  check(t.d.get_io$$receiveWindow()==65536,"large reassembly leaked credit");
  ++waves;
 }
 t.close(true);t.drain();
 std::cout<<"PRODUCTION_REASSEMBLY_PASS waves="<<waves<<" bytes="<<t.receivedBytes
  <<" cycles="<<t.cycles<<" payload=1460 retained_gap_segments=8"
    " rx_capacity=65536 wrap=pass shuffled=pass consumer_stall=pass checksum=pass\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
