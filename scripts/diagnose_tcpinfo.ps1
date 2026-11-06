$ErrorActionPreference='Stop'
$source=@"
using System;
using System.Diagnostics;
using System.Collections.Generic;
using System.Globalization;
using System.Net;
using System.Net.Sockets;
using System.Text;
public class TcpDiagSnapshot {
 public double AtSeconds;
 public int ReturnedBytes;
 public uint State,Mss,RttUs,MinRttUs,BytesInFlight,Cwnd,SndWnd,RcvWnd,RcvBuf,BytesReordered,BytesRetrans,FastRetrans,DupAcksIn,TimeoutEpisodes;
 public ulong ConnectionTimeMs,BytesOut,BytesIn;
 public bool TimestampsEnabled;
 public uint SndLimTransRwin,SndLimTimeRwin,SndLimTransCwnd,SndLimTimeCwnd,SndLimTransSnd,SndLimTimeSnd;
 public ulong SndLimBytesRwin,SndLimBytesCwnd,SndLimBytesSnd;
}
public class NativeSocketProbeResult {
 public long LocalBytes; public double LocalSeconds; public double CpuPercent;
 public int BlockBytes; public int SendBufferBytes; public bool NoDelay; public string ServerJson; public TcpDiagSnapshot[] TcpSamples;
}
public static class NativeSocketIperfProbe {
 static TcpDiagSnapshot Snapshot(Socket s,double seconds) {
  byte[] b=new byte[256];int n=s.IOControl(unchecked((int)0xd8000027),BitConverter.GetBytes((uint)1),b);
  if(n<136)throw new Exception("TCP_INFO_v1 returned "+n+" bytes, expected at least136");
  TcpDiagSnapshot x=new TcpDiagSnapshot();x.AtSeconds=seconds;x.ReturnedBytes=n;
  x.State=BitConverter.ToUInt32(b,0);x.Mss=BitConverter.ToUInt32(b,4);x.ConnectionTimeMs=BitConverter.ToUInt64(b,8);x.TimestampsEnabled=b[16]!=0;
  x.RttUs=BitConverter.ToUInt32(b,20);x.MinRttUs=BitConverter.ToUInt32(b,24);x.BytesInFlight=BitConverter.ToUInt32(b,28);x.Cwnd=BitConverter.ToUInt32(b,32);x.SndWnd=BitConverter.ToUInt32(b,36);x.RcvWnd=BitConverter.ToUInt32(b,40);x.RcvBuf=BitConverter.ToUInt32(b,44);
  x.BytesOut=BitConverter.ToUInt64(b,48);x.BytesIn=BitConverter.ToUInt64(b,56);x.BytesReordered=BitConverter.ToUInt32(b,64);x.BytesRetrans=BitConverter.ToUInt32(b,68);x.FastRetrans=BitConverter.ToUInt32(b,72);x.DupAcksIn=BitConverter.ToUInt32(b,76);x.TimeoutEpisodes=BitConverter.ToUInt32(b,80);
  x.SndLimTransRwin=BitConverter.ToUInt32(b,88);x.SndLimTimeRwin=BitConverter.ToUInt32(b,92);x.SndLimBytesRwin=BitConverter.ToUInt64(b,96);x.SndLimTransCwnd=BitConverter.ToUInt32(b,104);x.SndLimTimeCwnd=BitConverter.ToUInt32(b,108);x.SndLimBytesCwnd=BitConverter.ToUInt64(b,112);x.SndLimTransSnd=BitConverter.ToUInt32(b,120);x.SndLimTimeSnd=BitConverter.ToUInt32(b,124);x.SndLimBytesSnd=BitConverter.ToUInt64(b,128);return x;
 }
 static Socket Open() {
  Socket s=new Socket(AddressFamily.InterNetwork,SocketType.Stream,ProtocolType.Tcp);
  s.SendTimeout=5000; s.ReceiveTimeout=5000;
  IAsyncResult a=s.BeginConnect(new IPEndPoint(IPAddress.Parse("192.168.137.10"),5201),null,null);
  if(!a.AsyncWaitHandle.WaitOne(3000)){s.Close();throw new Exception("Connect timeout");}
  s.EndConnect(a);a.AsyncWaitHandle.Close();return s;
 }
 static void SendAll(Socket s,byte[] b) { int i=0;while(i<b.Length){int n=s.Send(b,i,b.Length-i,SocketFlags.None);if(n<=0)throw new Exception("Send EOF");i+=n;} }
 static byte[] ReadExact(Socket s,int count) { byte[] b=new byte[count];int i=0;while(i<count){int n=s.Receive(b,i,count-i,SocketFlags.None);if(n<=0)throw new Exception("Control EOF");i+=n;}return b; }
 static void State(Socket s,int expected) { int actual=ReadExact(s,1)[0];if(actual!=expected)throw new Exception("State "+actual+", expected "+expected); }
 static void SendJson(Socket s,string value) {byte[] b=Encoding.UTF8.GetBytes(value);SendAll(s,new byte[]{(byte)(b.Length>>24),(byte)(b.Length>>16),(byte)(b.Length>>8),(byte)b.Length});SendAll(s,b);}
 public static NativeSocketProbeResult Run(int blockBytes,int sendBuffer,bool noDelay,int seconds) {
  Socket control=null,data=null;
  try {
   control=Open();
   byte[] cookie=Encoding.ASCII.GetBytes("diag"+Guid.NewGuid().ToString("N")+"\0");
   SendAll(control,cookie);State(control,9);
   SendJson(control,"{\"tcp\":true,\"parallel\":1,\"time\":"+seconds+",\"len\":"+blockBytes+",\"reverse\":0,\"omit\":0,\"client_version\":\"native-socket-diagnostic\"}");
   State(control,10);
   data=Open();data.SendBufferSize=sendBuffer;data.NoDelay=noDelay;
   byte[] payload=new byte[blockBytes];for(int i=0;i<payload.Length;i++)payload[i]=(byte)i;
   SendAll(data,cookie);State(control,1);State(control,2);
   Process process=Process.GetCurrentProcess();TimeSpan cpuStart=process.TotalProcessorTime;
   long bytes=0;List<TcpDiagSnapshot> samples=new List<TcpDiagSnapshot>();double nextSample=double.MaxValue;Stopwatch watch=Stopwatch.StartNew();
   while(watch.Elapsed.TotalSeconds<seconds) {int n=data.Send(payload,0,payload.Length,SocketFlags.None);if(n<=0)throw new Exception("Data send EOF");bytes+=n;if(watch.Elapsed.TotalSeconds>=nextSample){samples.Add(Snapshot(data,watch.Elapsed.TotalSeconds));nextSample=watch.Elapsed.TotalSeconds+.1;}}
   watch.Stop();samples.Add(Snapshot(data,watch.Elapsed.TotalSeconds));double cpu=(process.TotalProcessorTime-cpuStart).TotalSeconds/watch.Elapsed.TotalSeconds*100;
   SendAll(control,new byte[]{4});State(control,13);
   string local="{\"cpu_util_total\":"+cpu.ToString("R",CultureInfo.InvariantCulture)+",\"cpu_util_user\":0,\"cpu_util_system\":0,\"sender_has_retransmits\":false,\"streams\":[{\"id\":1,\"bytes\":"+bytes+",\"retransmits\":-1,\"start_time\":0,\"end_time\":"+watch.Elapsed.TotalSeconds.ToString("R",CultureInfo.InvariantCulture)+"}]}";
   SendJson(control,local);
   byte[] h=ReadExact(control,4);int length=(h[0]<<24)|(h[1]<<16)|(h[2]<<8)|h[3];
   if(length<=0||length>4096)throw new Exception("Invalid result length "+length);
   string server=Encoding.UTF8.GetString(ReadExact(control,length));State(control,14);SendAll(control,new byte[]{16});
   NativeSocketProbeResult r=new NativeSocketProbeResult();r.LocalBytes=bytes;r.LocalSeconds=watch.Elapsed.TotalSeconds;r.CpuPercent=cpu;r.BlockBytes=blockBytes;r.SendBufferBytes=data.SendBufferSize;r.NoDelay=noDelay;r.ServerJson=server;r.TcpSamples=samples.ToArray();return r;
  } finally {if(data!=null)data.Dispose();if(control!=null)control.Dispose();}
 }
}
"@
Add-Type -TypeDefinition $source -Language CSharp
$out=Join-Path $PSScriptRoot ('rx256-native-tcpinfo-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ'));New-Item -ItemType Directory -Path $out | Out-Null
$utf8=[Text.UTF8Encoding]::new($false)
$cases=@(@{name='128K-buffer64K';block=131072;buffer=65536;nd=$false},@{name='32K-N-buffer1M';block=32768;buffer=1048576;nd=$true},@{name='64K-N-buffer1M';block=65536;buffer=1048576;nd=$true})
$rows=@()
foreach($case in $cases){
 Write-Output ('NATIVE_SOCKET_RUN '+$case.name)
 $r=[NativeSocketIperfProbe]::Run($case.block,$case.buffer,$case.nd,5)
 $server=$r.ServerJson|ConvertFrom-Json
 $secs=[double]$server.streams[0].end_time-[double]$server.streams[0].start_time
 $row=[ordered]@{name=$case.name;client='Native Windows .NET Socket/Winsock diagnostic, not the iperf3 executable';board_build_label='TX64-RX256K-N57M';block_bytes=$r.BlockBytes;send_buffer_bytes=$r.SendBufferBytes;nodelay=$r.NoDelay;local_socket_accepted_bytes=$r.LocalBytes;local_seconds=$r.LocalSeconds;local_send_mbps=$r.LocalBytes*8/$r.LocalSeconds/1e6;client_cpu_percent=$r.CpuPercent;hardware_receive_bytes=$server.streams[0].bytes;hardware_reported_seconds=$secs;hardware_receive_mbps=[double]$server.streams[0].bytes*8/$secs/1e6;server_result=$server;tcp_samples=@($r.TcpSamples);tcp_info_reference='https://learn.microsoft.com/en-us/windows/win32/api/mstcpip/ns-mstcpip-tcp_info_v1'}
 [IO.File]::WriteAllText((Join-Path $out ($case.name+'.json')),($row|ConvertTo-Json -Depth 8),$utf8)
 $rows+=$row;[IO.File]::WriteAllText((Join-Path $out 'summary.json'),($rows|ConvertTo-Json -Depth 8),$utf8)
 Write-Output (@{name=$row.name;receiver_mbps=$row.hardware_receive_mbps;cpu=$row.client_cpu_percent;last_tcp=$r.TcpSamples[-1]}|ConvertTo-Json -Depth 8 -Compress)
 Start-Sleep -Seconds 1
}
Write-Output ('RESULT_DIRECTORY '+$out)