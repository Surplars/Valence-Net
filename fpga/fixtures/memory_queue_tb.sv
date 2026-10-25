`timescale 1ns/1ps
module queue_tb;
reg clock=0, reset=1;
always #5 clock=~clock;
reg [1:0] select=0;
reg valid=0,outReady=0;
reg [63:0] lo=0,hi=0;
wire ready,outValid;
wire [63:0] outLo,outHi;
wire [4:0] count;
MemoryQueueHarness dut(.clock(clock),.reset(reset),.io_select(select),
.io_valid(valid),.io_ready(ready),.io_lo(lo),.io_hi(hi),
.io_outReady(outReady),.io_outValid(outValid),.io_outLo(outLo),.io_outHi(outHi),.io_count(count));
reg [127:0] expected[0:2][0:15];
integer size[0:2], head[0:2],tail[0:2],depth[0:2];
integer i,s,phase,overlap=0,forwarding=0,stalls=0;
reg enq,deq;
reg [127:0] value;
initial begin
depth[0]=2;depth[1]=3;depth[2]=16;
for(s=0;s<3;s=s+1) begin size[s]=0;head[s]=0;tail[s]=0;end
repeat(40) @(negedge clock);
reset=0;
for(i=0;i<50000;i=i+1) begin
@(negedge clock);
s=$urandom_range(0,2); select=s; phase=(i/64)%5;
valid=(phase==0 || phase==2 || (phase==4 && $urandom_range(0,3)>0));
outReady=(phase==1 || phase==2 || (phase==4 && $urandom_range(0,3)>0));
lo={$urandom,$urandom};hi={$urandom,$urandom};
#1;
if(count!==size[s] || ready!==(size[s]<depth[s]) || outValid!==(size[s]>0))
$fatal(1,"FIFO handshakes/count i=%0d s=%0d count=%0d expected=%0d",i,s,count,size[s]);
if(size[s]>0 && {outHi,outLo}!==expected[s][head[s]])
$fatal(1,"FIFO mapped RAM head corruption i=%0d s=%0d",i,s);
enq=valid&&ready;deq=outReady&&outValid;value={hi,lo};
if(enq && deq) overlap=overlap+1;
if(enq && (size[s]==0 || (size[s]==1 && deq))) forwarding=forwarding+1;
if(outValid && !outReady) stalls=stalls+1;
@(posedge clock);
if(deq) begin size[s]=size[s]-1;head[s]=(head[s]+1)%depth[s];end
if(enq) begin expected[s][tail[s]]=value;size[s]=size[s]+1;tail[s]=(tail[s]+1)%depth[s];end
end
if(overlap<1000 || forwarding<1000 || stalls<1000) $fatal(1,"insufficient coverage");
$display("MAPPED_MEMORY_QUEUE_PASS cycles=50000 simultaneous=%0d forwarding=%0d stalls=%0d",overlap,forwarding,stalls);
$finish;
end
initial begin #1000000;$fatal(1,"timeout");end
endmodule
