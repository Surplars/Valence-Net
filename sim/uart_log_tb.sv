`timescale 1ns/1ps
module uart_log_tb;
 localparam integer SERIAL_BIT_NS=271*8;
 localparam real RX_BIT_NS=1000000000.0/460800.0;
 reg clock=0,reset=1,link=0,rx_locked=0,reverse=0,net_fault=0;
 reg [4:0] phase=0;
 reg [63:0] rx_bytes=64'h0123456789abcdef,tx_bytes=64'hfedcba9876543210;
 reg [31:0] app_errors=0;
 reg [7:0] phy_fault=0;
 reg [607:0] diagnostics=0;
 wire uart_tx;
 // Production serializer/divisor; only heartbeat is shortened to 40ms.
 iperf_uart_log #(.HEARTBEAT_CYCLES(5000000)) dut(
  .clock(clock),.reset(reset),.link(link),.rx_locked(rx_locked),.phase(phase),
  .reverse(reverse),.rx_bytes(rx_bytes),.tx_bytes(tx_bytes),
  .app_errors(app_errors),.phy_fault(phy_fault),.net_fault(net_fault),
  .diagnostics(diagnostics),.uart_tx(uart_tx));
 always #4 clock=~clock;
 integer characters=0,lines=0,resets=0,changes=0;
 reg checking_frame=0;
 real frame_start=0,edge_delta,last_line_start=0,reset_release_time=0;
 integer edge_bit;
 always @(uart_tx)if(!reset)begin
   if(uart_tx!==0 && uart_tx!==1)$fatal(1,"Unknown serial output");
   if(checking_frame)begin
     edge_delta=$realtime-frame_start;
     edge_bit=$rtoi((edge_delta+0.001)/SERIAL_BIT_NS);
     if(edge_delta-edge_bit*SERIAL_BIT_NS>0.002)
       $fatal(1,"Serial edge away from exact 271-clock boundary: %0.3f",edge_delta);
   end
 end
 task automatic set_diagnostics(input reg [31:0] base);
   for(integer wordno=0;wordno<19;wordno=wordno+1)
     diagnostics[wordno*32+:32]=base+wordno;
 endtask
 function automatic string expected_diagnostics(input reg [31:0] base);
   string result;
   begin
     result=$sformatf("DIAG rx=%08x drop=%08x fcs=%08x why=",base,base+1,base+2);
     for(integer reason=0;reason<8;reason=reason+1)begin
       if(reason!=0)result={result,","};
       result={result,$sformatf("%08x",base+3+reason)};
     end
     expected_diagnostics={result,$sformatf(" stall=%08x max=%08x sr=%08x sb=%08x st=%08x tx=%08x rej=%08x ib=%08x\015\012",
       base+11,base+12,base+13,base+14,base+15,base+16,base+17,base+18)};
   end
 endfunction
 task automatic receive_byte(output reg [7:0] value);
  integer bitno;
  begin
   @(negedge uart_tx);
   frame_start=$realtime;checking_frame=1;
   #(RX_BIT_NS/2.0);
   if(uart_tx!==0)$fatal(1,"Missing start bit");
   for(bitno=0;bitno<8;bitno=bitno+1)begin
     #RX_BIT_NS;
     if(uart_tx!==0 && uart_tx!==1)$fatal(1,"Unknown data bit");
     value[bitno]=uart_tx;
   end
   #RX_BIT_NS;if(uart_tx!==1)$fatal(1,"Missing stop bit");
   checking_frame=0;characters=characters+1;
  end
 endtask
 task automatic receive_line(output string value,input integer change_during_line);
  reg [7:0] byte_value;
  integer count;
  begin
   value="";count=0;byte_value=0;
   while(byte_value!=10)begin
     receive_byte(byte_value);
     value={value,byte_value};count=count+1;
     if(count==1)last_line_start=frame_start;
     if(count>240)$fatal(1,"Overlong UART line");
     if(change_during_line==1 && count==12)begin
       link=1;rx_locked=1;phase=7;reverse=1;
       rx_bytes=64'h1111111111111111;tx_bytes=64'h2222222222222222;
       app_errors=32'h33333333;phy_fault=8'h04;net_fault=1;changes=changes+1;
     end
     if(change_during_line==1 && count==20)begin
       phase=5'h1c;rx_bytes=64'h89abcdef01234567;tx_bytes=64'h76543210fedcba98;
       app_errors=32'h1234abcd;phy_fault=8'h03;changes=changes+1;
     end
     if(change_during_line==2 && count==12)begin set_diagnostics(32'h23456780);changes=changes+1;end
     if(change_during_line==2 && count==20)begin set_diagnostics(32'habcdef00);changes=changes+1;end
   end
   lines=lines+1;
   $display("UART_LINE %s",value);
  end
 endtask
 string value;
 string header="VALENCE IPERF3 IP=192.168.137.10/24 GW=192.168.137.1 TCP=5201 UART=460800 8N1 BUILD=TX64-RX256K-N57B\015\012";
 string first="STAT link=0 rxLock=0 phase=00 reverse=0 rxBytes=0123456789abcdef txBytes=fedcba9876543210 appErrors=00000000 phyFault=00 netFault=0\015\012";
 string latest="STAT link=1 rxLock=1 phase=1c reverse=1 rxBytes=89abcdef01234567 txBytes=76543210fedcba98 appErrors=1234abcd phyFault=03 netFault=1\015\012";
 initial begin
   #80;
   if(uart_tx!==1)$fatal(1,"Reset must drive idle high");
   @(negedge clock);reset=0;reset_release_time=$realtime;
   receive_line(value,0);if(value!=header)$fatal(1,"Header mismatch");
   set_diagnostics(32'h10203040);
   receive_line(value,1);if(value!=first)$fatal(1,"Status snapshot tore during serialization");
   receive_line(value,2);if(value!=expected_diagnostics(32'h10203040))
     $fatal(1,"Diagnostic snapshot tore or word mapping is wrong");
   receive_line(value,0);if(value!=latest)$fatal(1,"Busy change did not coalesce to latest snapshot");
   receive_line(value,0);if(value!=latest)$fatal(1,"Heartbeat snapshot mismatch or counter changes triggered extra lines");
   if(last_line_start<reset_release_time+40000000-8 ||
      last_line_start>reset_release_time+40000000+32)
     $fatal(1,"Heartbeat first start bit not at expected 5000000-clock interval: %0.3f",last_line_start-reset_release_time);
   receive_line(value,0);if(value!=expected_diagnostics(32'habcdef00))
     $fatal(1,"Heartbeat did not capture latest diagnostics");
   // Interrupt the next heartbeat in the middle of a serial character.
   @(negedge uart_tx);#(2*SERIAL_BIT_NS);reset=1;resets=resets+1;
   #100;if(uart_tx!==1)$fatal(1,"Mid-byte reset failed to return idle");
   @(negedge clock);reset=0;
   receive_line(value,0);if(value!=header)$fatal(1,"Reset did not restart complete header");
   receive_line(value,0);if(value!=latest)$fatal(1,"Reset snapshot mismatch");
   link=0;rx_locked=0;phase=0;reverse=0;net_fault=0;
   rx_bytes=64'h0123456789abcdef;tx_bytes=64'hfedcba9876543210;app_errors=0;phy_fault=0;
   receive_line(value,0);if(value!=expected_diagnostics(32'habcdef00))
     $fatal(1,"Reset did not replay diagnostics");
   receive_line(value,0);
   if(value!=first)begin receive_line(value,0);if(value!=first)$fatal(1,"State change after reset missing");end
   $display("UART_LOG_PASS baud_div=271 clock_hz=125000000 lines=%0d bytes=%0d changes=%0d resets=%0d coherent=PASS framing=PASS heartbeat=PASS diagnostics=PASS",lines,characters,changes,resets);
   $finish;
 end
 initial begin #160000000;$fatal(1,"UART oracle timeout");end
endmodule
