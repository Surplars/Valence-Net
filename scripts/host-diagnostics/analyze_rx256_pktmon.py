from pathlib import Path
import bisect, collections, hashlib, json, struct
root=Path('/mnt/c/Users/surplars/Documents/Codex/2026-10-23/https-github-com-surplars-valence-tree-3/outputs/rx256-board-20261025T075630Z/pktmon-20261025T083040Z')
report=json.loads((root/'iperf3.json').read_text());port=report['start']['connected'][0]['local_port']
sent=[];acks=[];blockcounts=collections.Counter();interfaces=[];isn=None;scale=None
for filename in sorted(root.glob('capture*.pcapng')):
 with filename.open('rb') as f:
  while h:=f.read(8):
   kind,size=struct.unpack('<II',h);assert 12<=size<=10000000,(kind,size)
   b=f.read(size-8);assert len(b)==size-8 and struct.unpack_from('<I',b,len(b)-4)[0]==size
   blockcounts[kind]+=1
   if kind==1:interfaces.append(struct.unpack_from('<HHI',b))
   if kind!=6:continue
   iface,hi,lo,cap,orig=struct.unpack_from('<IIIII',b);stamp=((hi<<32)|lo)/1e6;p=b[20:20+cap]
   if len(p)<54 or p[12:14]!=b'\x08\x00':continue
   ip=14;ihl=(p[ip]&15)*4
   if p[ip+9]!=6:continue
   t=ip+ihl;sport,dport,seq,ack=struct.unpack_from('!HHII',p,t);off=(p[t+12]>>4)*4;flags=p[t+13];length=struct.unpack_from('!H',p,ip+2)[0]-ihl-off
   if {sport,dport}!={port,5201}:continue
   if sport==port:
    if flags&2:isn=seq
    if length:sent.append((stamp,seq,length,flags))
   elif flags&16 and not flags&4:
    if flags&2:
     opts=p[t+20:t+off];i=0
     while i<len(opts):
      k=opts[i]
      if k==0:break
      if k==1:i+=1;continue
      n=opts[i+1];assert n>=2
      if k==3:scale=opts[i+2]
      i+=n
    acks.append((stamp,ack,struct.unpack_from('!H',p,t+14)[0],flags))
assert isn is not None and scale is not None
sent.sort();acks.sort()
sent=[(t,(seq-isn-1)&0xffffffff,n,fl) for t,seq,n,fl in sent]
acks=[(t,(ack-isn-1)&0xffffffff,w if fl&2 else w<<scale,fl) for t,ack,w,fl in acks]
data=[x for x in sent if x[1]>=37];progress=[];last=0
for t,a,w,fl in acks:
 if a>last and a<0x80000000:progress.append((t,a,w,a-last));last=a
percent=lambda v:{str(p):sorted(v)[min(len(v)-1,int((len(v)-1)*p/100))] for p in [0,10,50,90,99,100]} if v else {}
tx_times=[x[0] for x in data];ack_times=[x[0] for x in progress]
tx_ends=[];m=0
for t,s,n,fl in data:m=max(m,s+n);tx_ends.append(m)
ack_values=[x[1] for x in progress]
# Cumulative ACK matching preserves GSO records as records, not physical packets.
sent_by_end=sorted((s+n,t,n) for t,s,n,fl in data);ends=[x[0] for x in sent_by_end]
rtts=[];flight=[];spare=[]
for t,a,w,delta in progress:
 j=bisect.bisect_left(ends,a)
 if j<len(ends) and ends[j]==a:rtts.append((t-sent_by_end[j][1])*1000)
 k=bisect.bisect_right(tx_times,t)-1
 outstanding=max(0,tx_ends[k]-a) if k>=0 else 0
 flight.append(outstanding);spare.append(w-outstanding)
gaps=[]
for i in range(1,len(data)):
 t0,s0,n0,_=data[i-1];t1,s1,n1,_=data[i];gap=(t1-t0)*1000
 if gap<.1:continue
 left=bisect.bisect_right(ack_times,t0);right=bisect.bisect_left(ack_times,t1)
 before=progress[left-1][1] if left else 0
 lastack=progress[right-1][1] if right else 0
 first=progress[left] if left<right else None
 gaps.append({'start_from_first_data_s':t0-data[0][0],'gap_ms':gap,'previous_record_bytes':n0,'next_record_bytes':n1,'outstanding_at_start':max(0,tx_ends[i-1]-before),'acks_in_gap':right-left,'ack_advance_in_gap':lastack-before,'first_ack_after_gap_start_ms':(first[0]-t0)*1000 if first else None,'last_ack_before_next_tx_ms':(t1-progress[right-1][0])*1000 if left<right else None,'spare_window_after_first_ack':first[2]-max(0,tx_ends[i-1]-first[1]) if first else None})
windows=[w for t,a,w,fl in acks if not fl&2]
ranges=sorted((s,s+n) for t,s,n,fl in sent);frontier=0;overlap=0;holes=0
for l,r in ranges:
 if l>frontier:holes+=1
 overlap+=max(0,min(frontier,r)-l);frontier=max(frontier,r)
summary={'port':port,'scale':scale,'packet_blocks':blockcounts[6],'interfaces':interfaces,'data_records':len(data),'ack_records':len(acks),'payload_size_histogram':collections.Counter(n for t,s,n,fl in data).most_common(12),'ack_advance_histogram':collections.Counter(x[3] for x in progress).most_common(12),'window_min_max':[min(windows),max(windows)],'zero_window_count':windows.count(0),'sequence_extent_including_cookie':frontier,'sequence_gap_count':holes,'sequence_overlap_bytes':overlap,'hardware_receive_bytes':report['end']['sum_received']['bytes'],'sender_bytes':report['end']['sum_sent']['bytes'],'sender_mbps':report['end']['sum_sent']['bits_per_second']/1e6,'receiver_mbps':report['end']['sum_received']['bits_per_second']/1e6,'capture_extent_matches_hardware_bytes':frontier-37==report['end']['sum_received']['bytes'],'ack_rtt_from_host_capture_ms_percentiles':percent(rtts),'tx_gap_ms_percentiles':percent([(data[i][0]-data[i-1][0])*1000 for i in range(1,len(data))]),'ack_gap_ms_percentiles':percent([(progress[i][0]-progress[i-1][0])*1000 for i in range(1,len(progress))]),'outstanding_at_ack_percentiles':percent(flight),'spare_window_at_ack_percentiles':percent(spare),'send_gaps_over_100us':len(gaps),'send_gaps_over_100us_total_ms':sum(x['gap_ms'] for x in gaps),'largest_send_gaps':sorted(gaps,key=lambda x:x['gap_ms'],reverse=True)[:30],'scope':'Windows PktMon NIC capture; timestamps include driver/offload/interrupt behavior and are not an external wire tap; no unconditional FPGA latency inference.'}
summary.update(final_tcp_ack_sequence_offset=max(ack_values), all_sender_tcp_payload_acknowledged=max(ack_values)>=frontier, acknowledged_payload_bytes_excluding_cookie=min(frontier,max(ack_values))-37)
(root/'packet-analysis.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k!='largest_send_gaps'},indent=2));print('TOP_GAPS',json.dumps(summary['largest_send_gaps'][:8],indent=2))