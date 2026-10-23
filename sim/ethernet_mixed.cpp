#include "EthernetTcpHarness.h"
#include "ethernet_peer.h"
using Test=EthernetPeer<SEthernetTcpHarness>;
int main(){try{
 Test t;t.active(1234,5678);t.mixed=true;t.allowedAck.push_back(t.peerNext);
 t.stallOutput=true;t.write(1024);
 std::vector<uint8_t> one{42};t.sendUdp(one);std::vector<uint8_t> two(128,99);t.sendUdp(two);
 uint32_t ack=t.d.get_io$$sendActual();
 for(unsigned i=0;i<60;++i){
  std::vector<uint8_t> payload(i%7==0?0:1+t.rng()%1400);for(auto&b:payload)b=t.rng();
  t.udpRxExpected.push_back(payload);t.ingress.push_back(t.udpFrame(payload));
  t.queueEcho(i%5==0?0:t.rng()%1400);
  if(i%6==0){
   std::vector<uint8_t> data(7+i%13);for(auto&b:data)b=t.rng();
   for(auto b:data)t.expectedRx.push_back(b);auto frame=t.encode(t.peerNext,ack,24,65535,data);
   t.peerNext+=data.size();t.allowedAck.push_back(t.peerNext);t.ingress.push_back(frame);
  }
 }
 t.stallOutput=false;
 for(unsigned i=0;i<80;++i){
  std::vector<uint8_t> payload(i%9==0?0:1+t.rng()%1400);for(auto&b:payload)b=t.rng();t.sendUdp(payload);
 }
 t.until([&]{return t.ingress.empty()&&t.udpTxExpected.empty()&&t.udpRxExpected.empty()&&t.echoExpected.empty()&&t.expectedRx.empty();},200000);
 t.until([&]{return t.d.get_io$$sendActual()==t.planned;});t.ack(t.planned);t.drain();
 check(t.udpTxPackets==82&&t.udpRxPackets==60&&t.echoPackets==60,"mixed protocol packet loss");
 unsigned drops=t.drops;t.queueEcho(63,true);t.until([&]{return t.ingress.empty();});t.idle(300);
 check(t.drops==drops+1&&t.echoExpected.empty(),"invalid ICMP checksum was answered");
 t.close(true);t.drain();
 std::cout<<"ETHERNET_MIXED_PASS cycles="<<t.cycles<<" udp_tx="<<t.udpTxPackets<<" udp_rx="<<t.udpRxPackets
  <<" echo_replies="<<t.echoPackets<<" tcp_packets="<<t.dataPackets<<" received_tcp_bytes="<<t.receivedBytes
  <<" shared_routing=pass arbitration=pass checksum=pass empty_payload=pass backpressure=pass\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
