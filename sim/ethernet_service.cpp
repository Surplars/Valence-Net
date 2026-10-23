#include "EthernetServiceHarness.h"
#include "ethernet_peer.h"
#include <iomanip>
using Test=EthernetPeer<SEthernetServiceHarness>;
static double rate(const std::vector<unsigned>&t,unsigned bytes,const char*name,unsigned payload){
 check(t.size()>=200,"too few sustained service samples");
 unsigned warm=32;double cycles=double(t.back()-t[warm])/(t.size()-1-warm);
 double gbps=double(bytes)*8.0*156.25/cycles/1000.0;
 std::cout<<std::fixed<<std::setprecision(4)<<"FULL_ENDPOINT_SERVICE path="<<name<<" payload="<<payload
  <<" cycles_per_packet="<<cycles<<" wire_equivalent_gbps_at_156_25MHz="<<gbps<<'\n';
 return gbps;
}
int main(){try{
 for(unsigned n:{0U,1U,17U,512U,1400U}){
  Test t;t.random=false;t.active(1000,2000);std::vector<uint8_t> payload(n,0xa5);
  for(unsigned i=0;i<256;++i)t.sendUdp(payload);
  t.until([&]{return t.udpTxExpected.empty();});t.drain();
  unsigned wireBytes=std::max(60U,42+n)+24;double tx=rate(t.udpTxEndTimes,wireBytes,"UDP_TX",n);
  for(unsigned i=0;i<256;++i){t.udpRxExpected.push_back(payload);t.ingress.push_back(t.udpFrame(payload));}
  t.until([&]{return t.udpRxExpected.empty();},200000);
  double rx=rate(t.udpRxEndTimes,wireBytes,"UDP_RX",n);
  check(tx>=10.0&&rx>=10.0,"UDP full endpoint does not sustain 10G");
 }
 for(unsigned n:{1U,64U,1460U}){
  Test t;t.random=false;t.active(10000,20000);t.autoPeerAck=true;
  for(unsigned i=0;i<256;++i)t.write(n);
  t.until([&]{return t.d.get_io$$sendOldest()==t.planned&&t.pending.empty();},200000);
  unsigned wireBytes=std::max(60U,54+n)+24;double tx=rate(t.dataEndTimes,wireBytes,"TCP_TX",n);
  check(tx>=10.0,"TCP full endpoint does not sustain 10G TX");
 }
 {
  Test t;t.random=false;t.active(30000,40000);uint32_t seq=t.peerNext;
  for(unsigned i=0;i<256;++i){std::vector<uint8_t> b{uint8_t(i)};t.expectedRx.push_back(i);
   t.ingress.push_back(t.encode(seq+i,t.planned,24,65535,b));}
  t.peerNext+=256;t.until([&]{return t.expectedRx.empty();},200000);t.drain();
  double rx=rate(t.tcpRxByteTimes,84,"TCP_RX",1);check(rx>=10.0,"TCP full endpoint does not sustain 10G minimum RX");
 }
 std::cout<<"FULL_ENDPOINT_SERVICE_PASS line_rate_target_gbps=10 raw_internal_gbps=20 clock_target_mhz=156.25 physical_timing_verified=false\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
