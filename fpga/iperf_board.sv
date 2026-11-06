`timescale 1ns/1ps
// XCZU15EG-F: 200MHz board clock, PL RJ2 RTL8211F, autonomous hardware.
module iperf_board (
 input wire clk_in1_p, clk_in1_n, sys_rst_n, button_n,
 input wire eth_rxc, eth_rx_ctl, input wire [3:0] eth_rxd,
 output wire eth_txc, eth_tx_ctl, output wire [3:0] eth_txd,
 output wire eth_mdc, eth_reset_gate, inout wire eth_mdio,
 output wire led,uart_tx
);
 wire board_reset=~sys_rst_n|~button_n;
 wire input_clock,feedback,feedback_buf,ui_source,ui_clock,input_locked;
 wire tx_clock,pad_clock,delay_clock,rx_clock,rx_pad,rx_source,rx_locked,locked;
 IBUFDS input_pad(.I(clk_in1_p),.IB(clk_in1_n),.O(input_clock));
 // Adapt the board's 200MHz input to the upstream clock component's 250MHz input.
 MMCME4_ADV #(.CLKIN1_PERIOD(5.0),.CLKFBOUT_MULT_F(5.0),.DIVCLK_DIVIDE(1),
  .CLKOUT0_DIVIDE_F(4.0),.COMPENSATION("AUTO")) clocks (
  .CLKIN1(input_clock),.CLKIN2(1'b0),.CLKINSEL(1'b1),.RST(board_reset),.PWRDWN(1'b0),
  .CLKFBOUT(feedback),.CLKFBIN(feedback_buf),.CLKOUT0(ui_source),
  .LOCKED(input_locked),.CLKFBOUTB(),.CLKOUT0B(),.CLKOUT1(),.CLKOUT1B(),
  .CLKOUT2(),.CLKOUT2B(),.CLKOUT3(),.CLKOUT3B(),.CLKOUT4(),.CLKOUT5(),.CLKOUT6(),
  .DCLK(1'b0),.DEN(1'b0),.DWE(1'b0),.DADDR(7'b0),.DI(16'b0),.DO(),.DRDY(),
  .PSCLK(1'b0),.PSEN(1'b0),.PSINCDEC(1'b0),.PSDONE(),
  .CDDCDONE(),.CDDCREQ(1'b0),.CLKINSTOPPED(),.CLKFBSTOPPED());
 BUFG feedback_buffer(.I(feedback),.O(feedback_buf));
 BUFG ui_buffer(.I(ui_source),.O(ui_clock));
 native_gmac_quarter_clock tx_clocks(.ui_clock(ui_clock),
  .cold_reset(board_reset|~input_locked),.tx_source(tx_clock),.tx_pad_source(pad_clock),
  .tx_forward_source(),.delay_source(delay_clock),.locked(locked));
 IBUF rx_pad_buffer(.I(eth_rxc),.O(rx_pad));
 native_gmac_rx_clock rx_clocks(.pad_clock(rx_pad),.cold_reset(board_reset),
  .clock_source(rx_source),.locked(rx_locked));
 BUFG rx_buffer(.I(rx_source),.O(rx_clock));
 wire clock_reset=board_reset|~locked;
 (* ASYNC_REG="TRUE" *) reg [2:0] delay_sync;
 always @(posedge delay_clock or posedge clock_reset)
   if(clock_reset)delay_sync<=3'b111;else delay_sync<={delay_sync[1:0],1'b0};
 reg [63:0] delay_guard;
 always @(posedge delay_clock or posedge delay_sync[2])
   if(delay_sync[2])delay_guard<=~64'b0;else delay_guard<={delay_guard[62:0],1'b0};
 wire delay_ready;
 wire calibration_reset=clock_reset|~delay_ready;
 (* ASYNC_REG="TRUE" *) reg [2:0] phy_sync;
 always @(posedge tx_clock or posedge calibration_reset)
   if(calibration_reset)phy_sync<=3'b111;else phy_sync<={phy_sync[1:0],1'b0};
 reg [20:0] phy_count;
 reg phy_released;
 always @(posedge tx_clock or posedge phy_sync[2])
   if(phy_sync[2])begin phy_count<=0;phy_released<=0;end
   else if(!phy_released)begin
     if(phy_count==21'd1249999)phy_released<=1;
     else phy_count<=phy_count+1'b1;
   end
 // Carrier NMOS inverts this output; high holds PHYRSTB low.
 assign eth_reset_gate=~phy_released;
 // The registered flag is asynchronously cleared by every calibration/reset cause.
 wire common_reset=eth_reset_gate;
 wire rx_reset_request=common_reset|~rx_locked;
 (* ASYNC_REG="TRUE" *) reg [2:0] tx_sync,rx_sync;
 always @(posedge tx_clock or posedge common_reset)
   if(common_reset)tx_sync<=7;else tx_sync<={tx_sync[1:0],1'b0};
 always @(posedge rx_clock or posedge rx_reset_request)
   if(rx_reset_request)rx_sync<=7;else rx_sync<={rx_sync[1:0],1'b0};
 wire [7:0] gmii_tx_data,gmii_rx_data;
 wire gmii_tx_enable,gmii_tx_error,gmii_rx_valid,gmii_rx_error,rx_inband_link;
 wire mdio_in,mdio_out,mdio_oe,link_up;
 (* ASYNC_REG="TRUE" *) reg mdio_meta,mdio_sync;
 always @(posedge tx_clock or posedge tx_sync[2])
   if(tx_sync[2])begin mdio_meta<=1;mdio_sync<=1;end
   else begin mdio_meta<=mdio_in;mdio_sync<=mdio_meta;end
 IOBUF mdio_pad(.I(mdio_out),.T(~mdio_oe),.O(mdio_in),.IO(eth_mdio));
 (* MARK_DEBUG="TRUE" *) wire [7:0] status;
 wire [4:0] debug_phase;
 wire debug_reverse;
 wire [63:0] debug_rx_bytes,debug_tx_bytes;
 wire [31:0] debug_errors;
 wire [7:0] debug_phy_fault;
 wire [607:0] debug_diagnostics;
 IperfBoardCore core(
  .txClock(tx_clock),.rxClock(rx_clock),.commonReset(common_reset),.rxClockLocked(rx_locked),
  .rxData(gmii_rx_data),.rxValid(gmii_rx_valid),.rxError(gmii_rx_error),.rxInbandLink(rx_inband_link),
  .txData(gmii_tx_data),.txEnable(gmii_tx_enable),.txError(gmii_tx_error),
  .mdioIn(mdio_sync),.mdioOut(mdio_out),.mdioOe(mdio_oe),.mdc(eth_mdc),
  .linkUp(link_up),.status(status),
  .debugPhase(debug_phase),.debugReverse(debug_reverse),
  .debugRxBytes(debug_rx_bytes),.debugTxBytes(debug_tx_bytes),
  .debugErrors(debug_errors),.debugPhyFault(debug_phy_fault),.debugDiagnostics(debug_diagnostics));
 // TX-domain logging remains alive when recovered RX clock is absent.
 iperf_uart_log logger(.clock(tx_clock),.reset(tx_sync[2]),
  .link(link_up),.rx_locked(rx_locked),.phase(debug_phase),.reverse(debug_reverse),
  .rx_bytes(debug_rx_bytes),.tx_bytes(debug_tx_bytes),.app_errors(debug_errors),
  .phy_fault(debug_phy_fault),.net_fault(status[7]),.diagnostics(debug_diagnostics),.uart_tx(uart_tx));
 // One real CLK250 drives ALL six output DDR cells. D1=D2 on data/control
 // means pad data changes only on positive CLK250 edges; TXC centers each nibble.
 native_rgmii #(.FPGA_TX_CLOCK_SHIFT(1),.TX_QUARTER_DDR(1),
  .ISOLATE_TX_PAD_CLOCK(1),.RX_DATA_DELAY_PS(0)) u_rgmii(
  .tx_clock(tx_clock),.rx_clock(rx_clock),.tx_reset(tx_sync[2]),.rx_reset(rx_sync[2]),
  .tx_pad_clock(pad_clock),.tx_forward_clock(tx_clock),.tx_forward_reset(tx_sync[2]),
  .delay_clock(delay_clock),.delay_reset(delay_guard[63]),.delay_ready(delay_ready),
  .gmii_tx_data(gmii_tx_data),.gmii_tx_enable(gmii_tx_enable),.gmii_tx_error(gmii_tx_error),
  .rgmii_rx_data(eth_rxd),.rgmii_rx_control(eth_rx_ctl),
  .rgmii_tx_data(eth_txd),.rgmii_tx_control(eth_tx_ctl),.rgmii_tx_clock(eth_txc),
  .gmii_rx_data(gmii_rx_data),.gmii_rx_valid(gmii_rx_valid),.gmii_rx_error(gmii_rx_error),
  .gigabit_link(rx_inband_link));
 assign led=link_up & rx_locked & ~status[7] & ~status[6];
endmodule
