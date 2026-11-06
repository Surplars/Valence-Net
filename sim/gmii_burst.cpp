// Deterministic GMII throughput/admission probe. Reuses the sealed GSIM model.
// Internal model fields are READ ONLY diagnostics; all stimuli enter GMII pins.
#include "IperfGmiiHarness.h"
#include <algorithm>
#include <deque>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>
#include <string>
#include <iostream>
using Bytes=std::vector<uint8_t>;
static void check(bool x,const std::string&s){if(!x)throw std::runtime_error(s);}
static unsigned u16(const Bytes&v,unsigned p){return(v.at(p)<<8)|v.at(p+1);}
static uint32_t u32(const Bytes&v,unsigned p){return(uint32_t(u16(v,p))<<16)|u16(v,p+2);}
static void p16(Bytes&v,unsigned p,unsigned x){v.at(p)=x>>8;v.at(p+1)=x;}
static void p32(Bytes&v,unsigned p,uint32_t x){p16(v,p,x>>16);p16(v,p+2,x);}
static uint16_t sum(const Bytes&v){unsigned s=0;for(unsigned i=0;i<v.size();i+=2)s+=(v[i]<<8)|(i+1<v.size()?v[i+1]:0);while(s>>16)s=(s&65535)+(s>>16);return~s;}
static uint32_t crc(const Bytes&v){uint32_t c=~0U;for(auto b:v){c^=b;for(int k=0;k<8;k++)c=(c>>1)^((c&1)?0xedb88320:0);}return~c;}
static constexpr uint32_t HOST=0xc0a88901,DUT=0xc0a8890a;
static const Bytes HM={2,0,0,0,0,1},DM={2,0,0,0,0,0x80};
static Bytes ethernet(unsigned type,const Bytes&payload){Bytes v=DM;v.insert(v.end(),HM.begin(),HM.end());v.push_back(type>>8);v.push_back(type);v.insert(v.end(),payload.begin(),payload.end());return v;}
static Bytes arp(bool reply){Bytes a(28);p16(a,0,1);p16(a,2,0x800);a[4]=6;a[5]=4;p16(a,6,reply?2:1);std::copy(HM.begin(),HM.end(),a.begin()+8);p32(a,14,HOST);std::copy(DM.begin(),DM.end(),a.begin()+18);p32(a,24,DUT);return ethernet(0x806,a);}
static Bytes tcp(unsigned port,uint32_t seq,uint32_t ack,unsigned flags,const Bytes&payload={},bool syn=false){
 Bytes opts=syn?Bytes{2,4,5,180,1,3,3,10,4,2,0,0}:Bytes{};
 Bytes t(20+opts.size()+payload.size());p16(t,0,port);p16(t,2,5201);p32(t,4,seq);p32(t,8,ack);t[12]=((20+opts.size())/4)<<4;t[13]=flags;p16(t,14,65535);
 std::copy(opts.begin(),opts.end(),t.begin()+20);std::copy(payload.begin(),payload.end(),t.begin()+20+opts.size());
 Bytes pseudo(12);p32(pseudo,0,HOST);p32(pseudo,4,DUT);pseudo[9]=6;p16(pseudo,10,t.size());pseudo.insert(pseudo.end(),t.begin(),t.end());p16(t,16,sum(pseudo));
 Bytes ip(20);ip[0]=0x45;p16(ip,2,20+t.size());p16(ip,4,1);p16(ip,6,0x4000);ip[8]=64;ip[9]=6;p32(ip,12,HOST);p32(ip,16,DUT);p16(ip,10,sum(ip));ip.insert(ip.end(),t.begin(),t.end());return ethernet(0x800,ip);
}
static Bytes wire(Bytes f){if(f.size()<60)f.resize(60);uint32_t c=crc(f);for(int i=0;i<4;i++)f.push_back(c>>(8*i));f.insert(f.begin(),{0x55,0x55,0x55,0x55,0x55,0x55,0x55,0xd5});return f;}
struct Packet{uint64_t cycle;unsigned port,flags;uint32_t seq,ack;Bytes payload;unsigned window;};
struct Peer {
 std::unique_ptr<SIperfGmiiHarness> model=std::make_unique<SIperfGmiiHarness>();
 SIperfGmiiHarness& d=*model;
 std::deque<Bytes> pending;
 Bytes tx;
 std::vector<Packet> packets;
 uint64_t cycle=0,macDrops=0,busyDrops=0,otherDrops=0;
 unsigned boardScale[2]={}; unsigned offset=0,gap=0,maxFlight=0;uint32_t hostSeq[2]={100000,200000},dutNext[2]={};
 bool allowAppErrors=false; bool autoAckControl=true,autoAckData=false; unsigned dataPackets=0;
 unsigned ackLatency=0; uint64_t wirePayloadBytes=0; uint32_t firstDataSequence=0;
 std::deque<std::pair<uint64_t,Bytes>> delayedAcks;
 Peer(){d.set_io$$link(1);d.set_io$$tick(0);d.set_io$$rxValid(0);d.set_io$$rxData(0);d.set_io$$rxError(0);d.set_reset(1);d.step();d.step();d.set_reset(0);idle(32);}
 void send(const Bytes&f){pending.push_back(wire(f));}
 void transmit(unsigned i,unsigned flags,const Bytes&payload={}){send(tcp(50000+i,hostSeq[i],dutNext[i],flags,payload,flags&2));hostSeq[i]+=payload.size()+((flags&2)?1:0);}
 void ack(unsigned i,uint32_t n){send(tcp(50000+i,hostSeq[i],n,16));}
 void handle(Bytes w){
  check(w.size()>=72 && w[7]==0xd5,"bad GMII frame");Bytes f(w.begin()+8,w.end()-4);uint32_t c=0;for(int i=0;i<4;i++)c|=uint32_t(w[w.size()-4+i])<<(8*i);check(c==crc(f),"bad TX FCS");
  if(u16(f,12)==0x806){if(u16(f,20)==1)send(arp(true));return;}
  if(u16(f,12)!=0x800||f[23]!=6)return;
  unsigned ip=14,th=ip+(f[ip]&15)*4,h=(f[th+12]>>4)*4,n=u16(f,16)-(f[ip]&15)*4-h,port=u16(f,th+2);
  check(port==50000||port==50001,"unexpected port");
  unsigned i=port-50000;Packet p{cycle,port,f[th+13],u32(f,th+4),u32(f,th+8),Bytes(f.begin()+th+h,f.begin()+th+h+n),u16(f,th+14)};
  packets.push_back(p);
  if(p.flags&2){dutNext[i]=p.seq+1;
   for(unsigned k=20;k<h;){unsigned kind=f[th+k];if(!kind)break;if(kind==1){k++;continue;}unsigned size=f[th+k+1];check(size>=2&&k+size<=h,"invalid SYN option");if(kind==3&&size==3)boardScale[i]=f[th+k+2];k+=size;}
   return;}
  if(n){check(p.seq==dutNext[i],"unexpected data gap/retransmission");dutNext[i]+=n;if(i==1){
    if(dataPackets==0)firstDataSequence=p.seq;
    for(unsigned j=0;j<n;j++){uint32_t k=p.seq-firstDataSequence+j;unsigned expected=((k/1460)*1472+k%1460)&255;check(p.payload[j]==expected,"retained payload corruption at byte "+std::to_string(k));}
    dataPackets++;wirePayloadBytes+=n;
   }
   if(i==0&&autoAckControl)ack(i,dutNext[i]);
   if(i==1&&autoAckData){if(ackLatency)delayedAcks.emplace_back(cycle+ackLatency,tcp(50001,hostSeq[1],dutNext[1],16));else ack(1,dutNext[1]);}}
 }
 void step(){
  while(!delayedAcks.empty()&&delayedAcks.front().first<=cycle){send(delayedAcks.front().second);delayedAcks.pop_front();}
  bool offering=!gap&&!pending.empty();
  d.set_io$$rxValid(offering);d.set_io$$rxData(offering?pending.front()[offset]:0);d.set_io$$tick(cycle%125000==124999);
  unsigned prior=d.stack$net$tcp$receiveState;bool eligible=d.stack$net$tcp$supported&&d.stack$net$tcp$found;
  d.step();cycle++;
  if(prior==1&&d.stack$net$tcp$receiveState==2&&!d.stack$net$tcp$routeValid){if(eligible)busyDrops++;else otherDrops++;}
  maxFlight=std::max(maxFlight,unsigned(d.stack$net$tcp$inFlight[1]));
  if(d.get_io$$rxDropped())macDrops++;
  check(!d.get_io$$fault()&&!d.get_io$$txError()&&(allowAppErrors||!d.get_io$$errors()),"DUT fault/error");
  if(offering){if(++offset==pending.front().size()){pending.pop_front();offset=0;gap=12;}}
  else if(gap)gap--;
  if(d.get_io$$txEnable())tx.push_back(d.get_io$$txData());else if(!tx.empty()){Bytes f;f.swap(tx);handle(f);}
 }
 void idle(unsigned n){while(n--)step();}
 void until(const std::function<bool()>&f,unsigned limit=200000){while(!f()&&limit--)step();check(f(),"timeout at cycle "+std::to_string(cycle)+" phase="+std::to_string(d.get_io$$phase())+" busyDrops="+std::to_string(busyDrops));}
 void start(bool reverse){
  send(arp(false));idle(2000);
  transmit(0,2);until([&]{return dutNext[0]!=0;});ack(0,dutNext[0]);Bytes cookie(37,'a');cookie.back()=0;transmit(0,24,cookie);
  until([&]{for(auto&p:packets)if(p.port==50000&&!p.payload.empty()&&p.payload[0]==9)return true;return false;});
  std::string json=std::string("{\"tcp\":true,\"parallel\":1,\"reverse\":")+(reverse?"true":"false")+"}";Bytes params(4);p32(params,0,json.size());params.insert(params.end(),json.begin(),json.end());transmit(0,24,params);
  until([&]{for(auto&p:packets)if(p.port==50000&&!p.payload.empty()&&p.payload[0]==10)return true;return false;});
  transmit(1,2);until([&]{return dutNext[1]!=0;});ack(1,dutNext[1]);transmit(1,24,cookie);
  until([&]{return d.get_io$$phase()==7;});std::cout<<"START reverse="<<reverse<<" cycle="<<cycle<<" busyDrops="<<busyDrops<<" macDrops="<<macDrops<<std::endl;
 }
 uint32_t oldest(){return d.stack$net$tcp$cores_1$connection$connection$control$oldest;}
 uint32_t actual(){return d.stack$net$tcp$cores_1$connection$connection$control$actual;}
};
int main(int argc,char**argv){try{
 std::string mode=argc>1?argv[1]:"reverse";unsigned banks=argc>2?std::stoul(argv[2]):64;Peer p;p.start(mode!="forward");uint64_t begin=p.cycle;
 if(mode=="late-reset"||mode=="active-reset"||mode=="control-reset"){
  p.allowAppErrors=true;p.autoAckData=true;p.until([&]{return p.dataPackets>=8;});
  if(mode!="active-reset"){
   p.transmit(0,24,Bytes{4});p.until([&]{return p.d.get_io$$phase()==9;});
   p.transmit(0,24,Bytes{0,0,0,2,123,125});p.until([&]{return p.d.get_io$$phase()==13;});
  }
  p.transmit(mode=="control-reset"?0:1,20);p.idle(10000);
  unsigned errors=p.d.get_io$$errors();
  if(mode=="late-reset"&&!errors){p.transmit(0,24,Bytes{16});p.until([&]{return p.d.get_io$$phase()==1;});}
  std::cout<<"PASS_CLOSE_PROBE mode="<<mode<<" banks="<<banks<<" errors="<<errors<<" final_phase="<<unsigned(p.d.get_io$$phase());
 }else if(mode=="window"){
  p.ackLatency=argc>3?std::stoul(argv[3]):32000;p.autoAckData=true;
  p.until([&]{return p.dataPackets>=banks*4;},8000000);
  uint64_t measuredBegin=p.cycle,bytesBegin=p.wirePayloadBytes;
  p.idle(500000);uint64_t bytes=p.wirePayloadBytes-bytesBegin;
  double mbps=double(bytes)*8*125/double(p.cycle-measuredBegin);
  check(bytes>0,"no wire progress");
  std::cout<<"PASS_ACK_LATENCY_WINDOW banks="<<banks<<" ack_latency_cycles="<<p.ackLatency<<" ack_latency_us="<<double(p.ackLatency)*.008<<" cycles_measured="<<p.cycle-measuredBegin<<" wire_payload_bytes="<<bytes<<" model_mbps="<<mbps;
 }else if(mode=="reverse"){
  p.autoAckData=true;p.until([&]{return p.dataPackets>=banks*4+32;},banks*10000+200000);p.autoAckData=false;
  p.idle(banks*2000+30000);p.until([&]{return p.pending.empty()&&p.gap==0;});
  unsigned bursts=2048/banks;
  for(unsigned burst=0;burst<bursts;burst++){
   uint32_t oldest=p.oldest(),end=p.dutNext[1];check(end-oldest==banks*1460,"expected full retained window: "+std::to_string(end-oldest));
   for(unsigned j=1;j<=banks;j++)p.ack(1,oldest+j*1460);
   p.until([&]{return p.pending.empty()&&p.gap==0;});p.until([&]{return p.oldest()==end;},4000);
   p.until([&]{return p.dutNext[1]==end+banks*1460;},banks*2000+20000);p.idle(300);
  }
  std::cout<<"PASS_ACK_BURST banks="<<banks<<" bursts="<<bursts<<" min_frame_start_gap_cycles=84 data_packets="<<p.dataPackets<<" oldest="<<p.oldest()<<" actual="<<p.actual();
 }else if(mode=="forward"){
  size_t start=p.packets.size();Bytes payload(1460,0x5a);for(unsigned i=0;i<512;i++)p.transmit(1,24,payload);
  p.until([&]{return p.pending.empty();},1000000);p.idle(20000);
  check(p.d.get_io$$rxBytes()==512ULL*1460,"forward bytes lost: "+std::to_string(p.d.get_io$$rxBytes()));
  unsigned acks=0,maxWindow=0;
  for(size_t i=start;i<p.packets.size();i++){auto&f=p.packets[i];if(f.port==50001&&f.payload.empty()&&(f.flags&16)){acks++;maxWindow=std::max(maxWindow,f.window<<p.boardScale[1]);}}
  std::cout<<"PASS_FORWARD_LINE_RATE banks="<<banks<<" packets=512 payload_bytes="<<p.d.get_io$$rxBytes()<<" ack_packets="<<acks<<" advertised_window_max="<<maxWindow<<" scale="<<p.boardScale[1];
 }else throw std::runtime_error("unknown mode");
 std::cout<<" cycles="<<p.cycle-begin<<" busy_drops="<<p.busyDrops<<" other_drops="<<p.otherDrops<<" mac_drops="<<p.macDrops<<" max_inflight="<<p.maxFlight<<" tick_period=125000"<<std::endl;
 check(p.busyDrops==0&&p.macDrops==0,"receive drops observed");return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}}
