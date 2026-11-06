`timescale 1ns/1ps
// Independent arithmetic oracle for the emitted production diagnostic CDC.
// Source=125MHz; destination is unrelated 76.923MHz and pauses during traffic.
module rx_diagnostics_tb;
 reg sourceClock=0,destinationClock=0,commonReset=1;
 reg source_running=1,destination_running=1;
 reg accepted=0,dropped=0,badFcs=0,backpressured=0,inbandLink=0;
 reg [7:0] dropReasons=0;
 wire [447:0] totals;
 RxDiagnosticSnapshot dut(
  .sourceClock(sourceClock),.destinationClock(destinationClock),.commonReset(commonReset),
  .accepted(accepted),.dropped(dropped),.badFcs(badFcs),.dropReasons(dropReasons),
  .backpressured(backpressured),.inbandLink(inbandLink),.totals(totals));
 always begin #4;if(source_running)sourceClock=~sourceClock;else sourceClock=0;end
 always begin #6.5;if(destination_running)destinationClock=~destinationClock;else destinationClock=0;end
 reg pattern_check=0;
 integer atomic_samples=0;
 reg [447:0] previous=0,paused_totals=0;
 integer n,remainder,expected_stall,expected_max,expected_falls;
 always @(posedge destinationClock)begin
  #1;
  if(!commonReset)begin
   if(^totals===1'bx)$fatal(1,"Unknown diagnostic output");
   if(pattern_check && totals!=previous)begin
     n=totals[31:0];
     if(n<0 || n>200000)$fatal(1,"Snapshot accepted outside generated sequence");
     if(totals[63:32]!=(n+1)/2 || totals[95:64]!=(n+3)/4)
       $fatal(1,"Atomic RX totals tore n=%0d",n);
     for(integer r=0;r<8;r=r+1)
       if(totals[(r+3)*32+:32]!=(n+3)/4)$fatal(1,"Atomic reason counters tore reason=%0d",r);
     remainder=n%16;expected_stall=7*(n/16)+((remainder<7)?remainder:7);
     expected_max=(n<7)?n:7;
     expected_falls=((n>1000)?1:0)+((n>3000)?1:0);
     if(totals[11*32+:32]!=expected_stall || totals[12*32+:32]!=expected_max ||
        totals[13*32+:32]!=expected_falls)
       $fatal(1,"Atomic stall/link counters tore n=%0d",n);
     atomic_samples=atomic_samples+1;
   end
  end
  previous=totals;
 end
 task automatic inputs_idle;
  begin accepted=0;dropped=0;badFcs=0;dropReasons=0;backpressured=0;end
 endtask
 task automatic check_final;
  begin
   if(totals[0*32+:32]!=200000 || totals[1*32+:32]!=100036 || totals[2*32+:32]!=50004)
     $fatal(1,"Event counts lost while destination clock was stopped");
   for(integer r=0;r<8;r=r+1)
     if(totals[(r+3)*32+:32]!=50001+r)$fatal(1,"Reason mapping/count mismatch %0d",r);
   if(totals[11*32+:32]!=87531 || totals[12*32+:32]!=31 || totals[13*32+:32]!=2)
     $fatal(1,"Stall total, longest stall, or link falling-edge total mismatch");
  end
 endtask
 initial begin
  #80;commonReset=0;
  // A full interval at initial link=0 must not synthesize a link-down event.
  repeat(140000)@(negedge sourceClock);
  if(totals!==448'b0)$fatal(1,"Initial low link generated a false drop");
  pattern_check=1;
  for(integer i=0;i<200000;i=i+1)begin
   @(negedge sourceClock);
   accepted=1;dropped=(i%2==0);badFcs=(i%4==0);
   dropReasons=(i%4==0)?8'hff:8'h00;
   backpressured=(i%16<7);
   inbandLink=!(i>=1000 && i<2000) && !(i>=3000 && i<4000);
   if(i==90000)begin destination_running=0;paused_totals=totals;end
   if(i==159999 && totals!==paused_totals)$fatal(1,"Output changed while destination clock stopped");
   if(i==160000)destination_running=1;
  end
  @(negedge sourceClock);inputs_idle();
  // Let the final pattern snapshot arrive and prove the asynchronous samples
  // agree with independent closed-form sequence totals before adding probes.
  repeat(140000)@(negedge sourceClock);
  if(atomic_samples<2)$fatal(1,"Too few asynchronous atomic snapshots exercised");
  if(totals[31:0]!=200000)$fatal(1,"Final accumulated total did not catch up");
  pattern_check=0;
  // Distinct reason totals make swaps detectable, beyond the atomic burst.
  for(integer r=0;r<8;r=r+1)begin
   dropped=1;dropReasons=8'b1<<r;badFcs=(r==3);
   repeat(r+1)@(negedge sourceClock);
   inputs_idle();
  end
  backpressured=1;repeat(31)@(negedge sourceClock);backpressured=0;
  repeat(140000)@(negedge sourceClock);
  check_final();
  // Stop RX: the destination must hold the last complete snapshot.
  source_running=0;paused_totals=totals;
  repeat(200)@(negedge destinationClock);
  if(totals!==paused_totals)$fatal(1,"Stopped source changed stable snapshot");
  commonReset=1;inbandLink=0;inputs_idle();
  #100;if(totals!==448'b0)$fatal(1,"Common reset did not clear destination with RX stopped");
  commonReset=0;
  #1000;if(totals!==448'b0)$fatal(1,"Reset release with RX stopped exposed stale epoch");
  source_running=1;
  repeat(140000)@(negedge sourceClock);
  if(totals!==448'b0)$fatal(1,"Stale counters or false link fall replayed after reset");
  $display("RX_DIAGNOSTICS_PASS accepted=200000 dropped=100036 badFcs=50004 stallCycles=87531 maxStall=31 inbandDrops=2 atomic_samples=%0d reason_mapping=PASS stopped_destination=PASS stopped_source=PASS common_reset=PASS",atomic_samples);
  $finish;
 end
 initial begin #12000000;$fatal(1,"Diagnostic oracle timeout");end
endmodule

