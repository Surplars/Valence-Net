from pathlib import Path
import datetime,json,os,signal,subprocess,time
root=Path('/home/openion/Valence-Iperf-Rx256K')
out=root/'build'/('board-rx256-linux-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ'));out.mkdir()
client=root/'tools/iperf3-src/src/iperf3';results=[]
for name,extra in [('forward-default',[]),('forward-32K-N',['-l','32K','-N'])]:
 case=out/name;case.mkdir();command=[str(client),'-c','192.168.137.10','-t','5','-J','--connect-timeout','3000',*extra]
 with (case/'capture.log').open('w') as capture_log:
  capture=subprocess.Popen(['tcpdump','--immediate-mode','-U','-i','eth0','-p','-n','-s','128','-B','8192','-w',str(case/'forward.pcap'),'tcp and host 192.168.137.10 and port 5201'],stderr=capture_log,stdout=subprocess.DEVNULL)
  try:
   time.sleep(.25);assert capture.poll() is None,'Capture did not start'
   with (case/'forward.json').open('w') as stdout,(case/'forward.stderr.txt').open('w') as stderr:
    completed=subprocess.run(command,stdout=stdout,stderr=stderr,timeout=18)
   time.sleep(.2)
  finally:
   if capture.poll() is None:capture.send_signal(signal.SIGINT)
   try:capture.wait(timeout=5)
   except subprocess.TimeoutExpired:capture.terminate();capture.wait(timeout=3)
 j=json.loads((case/'forward.json').read_text());e=j.get('end',{})
 intervals=[v['streams'][0] for v in j.get('intervals',[])]
 row={'case':name,'command':command,'exit_code':completed.returncode,'error':j.get('error'),'sender_mbps':e.get('sum_sent',{}).get('bits_per_second',0)/1e6,'receiver_mbps':e.get('sum_received',{}).get('bits_per_second',0)/1e6,'retransmits':e.get('sum_sent',{}).get('retransmits'),'cpu':e.get('cpu_utilization_percent'),'max_cwnd':max((i.get('snd_cwnd',0) for i in intervals),default=0),'max_snd_wnd':max((i.get('snd_wnd',0) for i in intervals),default=0),'rtt_us':[i.get('rtt') for i in intervals],'path':'Linux client through WSL NAT; different software/network path from native Windows'}
 results.append(row);(out/'summary.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(row),flush=True)
 if completed.returncode or j.get('error'):break
 time.sleep(1)
for p in [out,*out.rglob('*')]:os.chown(p,1000,1000)
print('RESULT_DIRECTORY',out,flush=True)