`timescale 1ps/1ps
// Exercise the production GMII MAC, asynchronous bridge and 32->128 packer.
// CRC, expected frame bytes and frame boundaries are independent of the RTL.
module rx_bridge_tb;
 reg rx_clock=0,tx_clock=0,common_reset=1;
 integer rx_half_ps=4001;
 always #(rx_half_ps) rx_clock=~rx_clock;
 always #4000 tx_clock=~tx_clock;
 wire rx_reset,tx_reset;
 CdcResetRelease rx_release(.clockIn(rx_clock),.asyncReset(common_reset),.resetOut(rx_reset));
 CdcResetRelease tx_release(.clockIn(tx_clock),.asyncReset(common_reset),.resetOut(tx_reset));
 reg [7:0] rx_data=0;
 reg rx_valid=0;
 wire frame_ready,frame_valid,frame_last,accepted,dropped,bad_fcs;
 wire [31:0] frame_data;
 wire [3:0] frame_keep;
 wire [7:0] drop_reasons;
 GmiiFrameRx mac(.clock(rx_clock),.reset(rx_reset),.io_gmiiData(rx_data),
  .io_gmiiValid(rx_valid),.io_gmiiError(1'b0),.io_dropReasons(drop_reasons),
  .io_enable(1'b1),.io_frame_ready(frame_ready),.io_frame_valid(frame_valid),
  .io_frame_bits_data(frame_data),.io_frame_bits_keep(frame_keep),
  .io_frame_bits_last(frame_last),.io_accepted(accepted),.io_dropped(dropped),.io_badFcs(bad_fcs));
 wire bridge_ready,bridge_valid,bridge_last,bridge_bad;
 wire [31:0] bridge_data;
 wire [3:0] bridge_keep;
 EthernetFrameClockBridge crossing(.sourceClock(rx_clock),.destinationClock(tx_clock),
  .commonReset(common_reset),.source_ready(frame_ready),.source_valid(frame_valid),
  .source_bits_data(frame_data),.source_bits_keep(frame_keep),.source_bits_last(frame_last),
  .destination_ready(bridge_ready),.destination_valid(bridge_valid),
  .destination_bits_data(bridge_data),.destination_bits_keep(bridge_keep),
  .destination_bits_last(bridge_last),.destination_bits_bad(bridge_bad));
 wire out_valid,out_last,out_error;
 wire [127:0] out_data;
 wire [15:0] out_keep;
 integer tx_cycles=0;
 // Periodically stop the consumer for 200 cycles while RX remains at line rate.
 wire out_ready=!tx_reset && (tx_cycles%1024>=200);
 MacPack packer(.clock(tx_clock),.reset(tx_reset),.io_in_ready(bridge_ready),
  .io_in_valid(bridge_valid),.io_in_bits_data(bridge_data),.io_in_bits_keep(bridge_keep),
  .io_in_bits_last(bridge_last),.io_in_bits_bad(bridge_bad),.io_out_ready(out_ready),
  .io_out_valid(out_valid),.io_out_bits_data(out_data),.io_out_bits_keep(out_keep),
  .io_out_bits_last(out_last),.io_out_bits_error(out_error));
 integer phase=0,received_frames=0,position=0,accepted_frames=0;
 integer source_stalls=0,bytes_checked=0,phases_passed=0;
 function automatic [7:0] expected_byte(input integer frame,input integer pos);
  case(pos)
   0:expected_byte=8'h02; 1,2,3,4:expected_byte=0; 5:expected_byte=8'h80;
   6:expected_byte=8'h02; 7,8,9,10:expected_byte=0; 11:expected_byte=8'h01;
   12:expected_byte=8'h08; 13:expected_byte=0;
   default:expected_byte=(frame*73+pos*17+(pos>>8)*29)&255;
  endcase
 endfunction
 function automatic [31:0] crc_byte(input [31:0] prior,input [7:0] data);
  reg [31:0] c;
  begin
   c=prior^data;
   for(integer bit_index=0;bit_index<8;bit_index=bit_index+1)
    c=(c>>1)^((c&1)?32'hedb88320:0);
   crc_byte=c;
  end
 endfunction
 always @(posedge rx_clock)if(!rx_reset)begin
  if(accepted)accepted_frames=accepted_frames+1;
  if(dropped || bad_fcs || drop_reasons!=0)
   $fatal(1,"Unexpected MAC drop phase=%0d reasons=%02x accepted=%0d",phase,drop_reasons,accepted_frames);
  if(frame_valid && !frame_ready)source_stalls=source_stalls+1;
 end
 always @(posedge tx_clock)begin
  if(tx_reset)tx_cycles<=0;else tx_cycles<=tx_cycles+1;
  if(!tx_reset && out_valid && out_ready)begin
   if(out_error || ^out_keep===1'bx || out_keep==0)$fatal(1,"Bad/unknown packed output");
   for(integer lane=0;lane<16;lane=lane+1)if(out_keep[lane])begin
    if(position>=1514 || out_data[lane*8+:8]!==expected_byte(phase*512+received_frames,position))
     $fatal(1,"Cross-domain corruption phase=%0d frame=%0d byte=%0d got=%02x expected=%02x",phase,received_frames,position,out_data[lane*8+:8],expected_byte(phase*512+received_frames,position));
    position=position+1;bytes_checked=bytes_checked+1;
   end
   if(out_last)begin
    if(position!=1514)$fatal(1,"Wrong frame length %0d",position);
    position=0;received_frames=received_frames+1;
   end else if(position==1514)$fatal(1,"Missing final marker");
  end
 end
 task automatic send_byte(input [7:0] data,input bit valid_byte);
  begin @(negedge rx_clock);rx_data=data;rx_valid=valid_byte;end
 endtask
 task automatic send_frame(input integer number);
  reg [31:0] crc;
  reg [7:0] data;
  begin
   for(integer n=0;n<7;n=n+1)send_byte(8'h55,1);
   send_byte(8'hd5,1);crc=32'hffffffff;
   for(integer n=0;n<1514;n=n+1)begin
    data=expected_byte(number,n);crc=crc_byte(crc,data);send_byte(data,1);
   end
   crc=~crc;
   for(integer n=0;n<4;n=n+1)send_byte(crc[n*8+:8],1);
   for(integer n=0;n<12;n=n+1)send_byte(0,0);
  end
 endtask
 initial begin
  for(phase=0;phase<2;phase=phase+1)begin
   common_reset=1;rx_valid=0;rx_half_ps=(phase==0)?4001:3999;
   #80000;received_frames=0;position=0;accepted_frames=0;source_stalls=0;
   common_reset=0;repeat(32)@(negedge rx_clock);
   for(integer f=0;f<512;f=f+1)send_frame(phase*512+f);
   send_byte(0,0);
   for(integer t=0;t<20000 && received_frames<512;t=t+1)@(negedge tx_clock);
   if(received_frames!=512 || accepted_frames!=512 || position!=0)
    $fatal(1,"Lost frames phase=%0d accepted=%0d received=%0d",phase,accepted_frames,received_frames);
   $display("RX_BRIDGE_PHASE_PASS half_period_ps=%0d frames=512 bytes=775168 source_stalls=%0d",rx_half_ps,source_stalls);
   phases_passed=phases_passed+1;
  end
  $display("RX_BRIDGE_PASS phases=%0d frames=1024 bytes=%0d CRC=PASS ordered=PASS backpressure=PASS ppm=PASS",phases_passed,bytes_checked);
  $finish;
 end
 initial begin #(64'd20000000000);$fatal(1,"RX bridge test timeout");end
endmodule
