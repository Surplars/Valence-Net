// Boundary-only oracle for production board RTL and Xilinx primitives.
// Full packet/TCP/iperf3 behavior is independently verified by GSIM + TAP.
`timescale 1ns/1ps
module IperfBoardCore(
 input wire txClock,rxClock,commonReset,rxClockLocked,input wire [7:0] rxData,
 input wire rxValid,rxError,rxInbandLink,mdioIn,output wire [7:0] txData,status,
 output wire txEnable,txError,mdioOut,mdioOe,mdc,linkUp,
 output wire [4:0] debugPhase,output wire debugReverse,
 output wire [63:0] debugRxBytes,debugTxBytes,
 output wire [31:0] debugErrors,output wire [7:0] debugPhyFault,
 output wire [607:0] debugDiagnostics
);
 assign txData=board_reset_tb.payload_data;
 assign txEnable=board_reset_tb.payload_enable;
 assign status=0;assign txError=0;assign mdioOut=1;assign mdioOe=0;
 assign mdc=0;assign linkUp=0;
 assign debugPhase=0;assign debugReverse=0;assign debugRxBytes=0;assign debugTxBytes=0;
 assign debugErrors=0;assign debugPhyFault=0;assign debugDiagnostics=0;
endmodule
module board_reset_tb;
 reg clk=0,reset_n=0,button_n=1,rxc=0,rx_running=0;
 // RGMII in-band status: duplex=1, speed=10 (1G), link=1 => 0xD.
 reg rxctl=0;reg [3:0] rxd=4'hd;
 reg [7:0] payload_data=0;reg payload_enable=0;
 always #2.5 clk=~clk;
 always #4 if(rx_running)rxc=~rxc;else rxc=0;
 tri1 mdio;
 wire txc,txctl,mdc,phy_reset,led,uart_tx;
 wire [3:0] txd;
 iperf_board dut(.clk_in1_p(clk),.clk_in1_n(~clk),.sys_rst_n(reset_n),.button_n(button_n),
 .eth_rxc(rxc),.eth_rx_ctl(rxctl),.eth_rxd(rxd),
 .eth_txc(txc),.eth_tx_ctl(txctl),.eth_txd(txd),.eth_mdc(mdc),
 .eth_reset_gate(phy_reset),.eth_mdio(mdio),.led(led),.uart_tx(uart_tx));
 function [7:0] tx_pattern(input integer n);tx_pattern=(n*37+90)&255;endfunction
 function [7:0] rx_pattern(input integer n);rx_pattern=(n*53+165)&255;endfunction
 integer release_cycles=0,release_count=0,tx_seen=0,rx_seen=0;
 real last_tx=0,last_pad=0,last_txc=0;
 reg test_tx_active=0,test_rx_active=0,low_valid=0;
 reg [3:0] low_nibble=0;
 always @(posedge dut.tx_clock) begin
   if(!dut.tx_sync[2])begin
     if(last_tx>0 && ($realtime-last_tx<7.99 || $realtime-last_tx>8.01))
       $fatal(1,"Word clock period must be 8 ns");
     last_tx=$realtime;
   end else last_tx=0;
   if(dut.phy_sync[2])release_cycles=0;
   else if(phy_reset)release_cycles=release_cycles+1;
 end
 always @(posedge dut.pad_clock)last_pad=$realtime;
 // D1=D2 makes all data/control output transitions positive-edge-only.
 // UNISIM has 100ps output propagation; 0.5ns classifies edges, well below the 2ns half-period.
 always @(txd or txctl)
   if(test_tx_active && last_pad>0 && $realtime-last_pad>0.5)
     $fatal(1,"TX symbol changed away from positive CLK250 edge: dt=%0.3f",$realtime-last_pad);
 always @(posedge txc)if(test_tx_active)begin
   if(last_txc>0 && ($realtime-last_txc<7.99 || $realtime-last_txc>8.01))
     $fatal(1,"Physical TXC period must be 8 ns");
   last_txc=$realtime;
   low_valid=txctl;
   low_nibble=txd;
 end
 always @(negedge txc)if(test_tx_active && low_valid)begin
   if(!txctl || tx_seen>=64 || {txd,low_nibble}!==tx_pattern(tx_seen))
     $fatal(1,"Physical TX byte/control mismatch at %0d: got %02x expected %02x",tx_seen,{txd,low_nibble},tx_pattern(tx_seen));
   tx_seen=tx_seen+1;low_valid=0;
 end
 always @(posedge dut.rx_clock)begin
   #0.1;
   if(test_rx_active && dut.gmii_rx_valid)begin
     if(dut.gmii_rx_error || rx_seen>=64 || dut.gmii_rx_data!==rx_pattern(rx_seen))
       $fatal(1,"Physical RX byte/control mismatch at %0d",rx_seen);
     rx_seen=rx_seen+1;
   end
 end
 always @(negedge phy_reset)begin
   if(!dut.delay_ready || release_cycles!=1250000)
     $fatal(1,"PHY released before calibration / exact 10ms: %0d",release_cycles);
   release_count=release_count+1;
 end
 integer i;
 reg [7:0] incoming;
 initial begin
   #200 reset_n=1;
   wait(!phy_reset);
   #1;
   if(dut.rx_sync!==3'b111 || dut.rx_locked!==1'b0)
     $fatal(1,"RX reset must remain held with RXC absent");
   rx_running=1;
   wait(dut.rx_locked);
   repeat(16)@(posedge dut.rx_clock);
   #1;
   if(dut.rx_sync!==0 || dut.tx_sync!==0)$fatal(1,"Synchronous reset release failed");
   test_tx_active=1;
   for(i=0;i<64;i=i+1)begin
     @(negedge dut.tx_clock);payload_enable=1;payload_data=tx_pattern(i);
   end
   @(negedge dut.tx_clock);payload_enable=0;payload_data=0;
   wait(tx_seen==64);
   repeat(6)@(posedge txc);
   test_tx_active=0;
   test_rx_active=1;
   // Independent PHY stimulus: data changes two ns after each RXC edge.
   // The RXC itself is the delayed clock supplied by the PHY.
   @(negedge rxc);#2;
   for(i=0;i<64;i=i+1)begin
     incoming=rx_pattern(i);rxd=incoming[3:0];rxctl=1;
     #4;rxd=incoming[7:4];
     #4;
   end
   rxctl=0;rxd=4'hd;
   repeat(16)@(posedge dut.rx_clock);
   if(rx_seen!=64)$fatal(1,"RX frame truncated: %0d",rx_seen);
   test_rx_active=0;
   force dut.delay_ready=1'b0;
   #40;
   if(!phy_reset || !dut.common_reset || dut.rx_sync!==3'b111)
     $fatal(1,"Calibration loss did not reassert resets");
   release dut.delay_ready;
   wait(!phy_reset);
   wait(dut.rx_locked);
   repeat(8)@(posedge dut.rx_clock);
   #1;
   if(release_count!=2 || dut.rx_sync!==0)$fatal(1,"Recovery failed");
   if(dut.rx_clocks.mmcm.CLKOUT0_PHASE!=45.0)$fatal(1,"Expected corrected45deg RX capture phase");
   $display("BOARD_RX_PHASE degrees=%0.3f",dut.rx_clocks.mmcm.CLKOUT0_PHASE);
   $display("BOARD_RESET_PASS releases=%0d cycles_each=1250000 TX125=PASS DDR250_positive_only=PASS TX_bytes=%0d RX_bytes=%0d RX_stopped=PASS calibration_loss=PASS",release_count,tx_seen,rx_seen);
   $finish;
 end
 initial begin #25000000;$fatal(1,"Board boundary timeout");end
endmodule
