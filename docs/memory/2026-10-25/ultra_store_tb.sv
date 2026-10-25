`timescale 1ns/1ps
module ultra_store_tb;
reg clock=0,reset=1;
always #5 clock=~clock;
reg [9:0] wa=0,ra=0;
reg [127:0] wd=0;
reg we=0,re=0;
wire [127:0] rd;
UltraStoreProbe dut(.clock(clock),.reset(reset),.io_wa(wa),.io_ra(ra),
.io_wd(wd),.io_we(we),.io_re(re),.io_rd(rd));
reg [127:0] expected[0:1023];
reg [127:0] value;
integer i,reads=0,writes=0;
initial begin
repeat(40) @(negedge clock);reset=0;
for(i=0;i<1024;i=i+1) begin
@(negedge clock);wa=i;we=1;re=0;wd={$urandom,$urandom,$urandom,$urandom};
expected[i]=wd;
@(posedge clock);#1;
end
for(i=0;i<10000;i=i+1) begin
@(negedge clock);
wa=$urandom_range(0,1023);ra=$urandom_range(0,1023);
if(wa==ra)ra=ra^1; // Payload ownership excludes same-address reads/writes.
we=$urandom_range(0,3)>0;re=$urandom_range(0,3)>0;
wd={$urandom,$urandom,$urandom,$urandom};value=expected[ra];
@(posedge clock);#1;
if(re)begin
if(rd!==value)$fatal(1,"URAM data/one-cycle latency mismatch i=%0d address=%0d",i,ra);
reads=reads+1;
end
if(we)begin expected[wa]=wd;writes=writes+1;end
end
if(reads<5000 || writes<5000)$fatal(1,"insufficient URAM coverage");
$display("MAPPED_ULTRA_STORE_PASS cycles=10000 reads=%0d writes=%0d",reads,writes);
$finish;
end
initial begin #200000;$fatal(1,"timeout");end
endmodule
