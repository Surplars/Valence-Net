`timescale 1ns/1ps
// Sampled diagnostic output only: no network ready/backpressure dependency.
// Snapshots are immutable while serialized. Changes during a line coalesce;
// this is not a lossless trace of every one-cycle network transition.
module iperf_uart_log #(
 parameter integer CLOCK_HZ=125000000,
 parameter integer BAUD_DIV=271,
 parameter integer HEARTBEAT_CYCLES=CLOCK_HZ
)(
 input wire clock,reset,
 input wire link,rx_locked,
 input wire [4:0] phase,
 input wire reverse,
 input wire [63:0] rx_bytes,tx_bytes,
 input wire [31:0] app_errors,
 input wire [7:0] phy_fault,
 input wire net_fault,
 input wire [607:0] diagnostics,
 output wire uart_tx
);
 localparam integer HB_WIDTH=$clog2(HEARTBEAT_CYCLES+1);
 localparam integer BAUD_WIDTH=$clog2(BAUD_DIV+1);
 localparam integer HEADER_BYTES=102;
 localparam integer STATUS_BYTES=133;
 localparam integer DIAGNOSTIC_BYTES=222;
 localparam [1:0] HEADER=0,STATUS=1,IDLE=2,DIAGNOSTIC=3;
 (* ASYNC_REG="TRUE" *) reg rx_lock_meta,rx_lock_sync;
 always @(posedge clock or posedge reset)
   if(reset)begin rx_lock_meta<=0;rx_lock_sync<=0;end
   else begin rx_lock_meta<=rx_locked;rx_lock_sync<=rx_lock_meta;end
 reg [HB_WIDTH-1:0] heartbeat;
 wire heartbeat_due=(heartbeat==HEARTBEAT_CYCLES-1);
 always @(posedge clock or posedge reset)
   if(reset)heartbeat<=0;
   else if(heartbeat_due)heartbeat<=0;
   else heartbeat<=heartbeat+1'b1;
 wire [48:0] observed={link,rx_lock_sync,phase,reverse,app_errors,phy_fault,net_fault};
 reg [48:0] previous;
 reg pending,diagnostic_pending;
 reg [607:0] s_diagnostics;
 reg [1:0] state;
 reg [7:0] character_index;
 reg s_link,s_rx_locked,s_reverse,s_net_fault;
 reg [4:0] s_phase;
 reg [63:0] s_rx_bytes,s_tx_bytes;
 reg [31:0] s_errors;
 reg [7:0] s_phy_fault;
 function automatic [7:0] hex_digit(input [3:0] n);
   hex_digit=(n<10)?8'h30+n:8'h61+(n-10);
 endfunction
 function automatic [7:0] header_char(input [7:0] n);
   case(n)
     8'd0:header_char=8'h56;
     8'd1:header_char=8'h41;
     8'd2:header_char=8'h4c;
     8'd3:header_char=8'h45;
     8'd4:header_char=8'h4e;
     8'd5:header_char=8'h43;
     8'd6:header_char=8'h45;
     8'd7:header_char=8'h20;
     8'd8:header_char=8'h49;
     8'd9:header_char=8'h50;
     8'd10:header_char=8'h45;
     8'd11:header_char=8'h52;
     8'd12:header_char=8'h46;
     8'd13:header_char=8'h33;
     8'd14:header_char=8'h20;
     8'd15:header_char=8'h49;
     8'd16:header_char=8'h50;
     8'd17:header_char=8'h3d;
     8'd18:header_char=8'h31;
     8'd19:header_char=8'h39;
     8'd20:header_char=8'h32;
     8'd21:header_char=8'h2e;
     8'd22:header_char=8'h31;
     8'd23:header_char=8'h36;
     8'd24:header_char=8'h38;
     8'd25:header_char=8'h2e;
     8'd26:header_char=8'h31;
     8'd27:header_char=8'h33;
     8'd28:header_char=8'h37;
     8'd29:header_char=8'h2e;
     8'd30:header_char=8'h31;
     8'd31:header_char=8'h30;
     8'd32:header_char=8'h2f;
     8'd33:header_char=8'h32;
     8'd34:header_char=8'h34;
     8'd35:header_char=8'h20;
     8'd36:header_char=8'h47;
     8'd37:header_char=8'h57;
     8'd38:header_char=8'h3d;
     8'd39:header_char=8'h31;
     8'd40:header_char=8'h39;
     8'd41:header_char=8'h32;
     8'd42:header_char=8'h2e;
     8'd43:header_char=8'h31;
     8'd44:header_char=8'h36;
     8'd45:header_char=8'h38;
     8'd46:header_char=8'h2e;
     8'd47:header_char=8'h31;
     8'd48:header_char=8'h33;
     8'd49:header_char=8'h37;
     8'd50:header_char=8'h2e;
     8'd51:header_char=8'h31;
     8'd52:header_char=8'h20;
     8'd53:header_char=8'h54;
     8'd54:header_char=8'h43;
     8'd55:header_char=8'h50;
     8'd56:header_char=8'h3d;
     8'd57:header_char=8'h35;
     8'd58:header_char=8'h32;
     8'd59:header_char=8'h30;
     8'd60:header_char=8'h31;
     8'd61:header_char=8'h20;
     8'd62:header_char=8'h55;
     8'd63:header_char=8'h41;
     8'd64:header_char=8'h52;
     8'd65:header_char=8'h54;
     8'd66:header_char=8'h3d;
     8'd67:header_char=8'h34;
     8'd68:header_char=8'h36;
     8'd69:header_char=8'h30;
     8'd70:header_char=8'h38;
     8'd71:header_char=8'h30;
     8'd72:header_char=8'h30;
     8'd73:header_char=8'h20;
     8'd74:header_char=8'h38;
     8'd75:header_char=8'h4e;
     8'd76:header_char=8'h31;
     8'd77:header_char=8'h20;
     8'd78:header_char=8'h42;
     8'd79:header_char=8'h55;
     8'd80:header_char=8'h49;
     8'd81:header_char=8'h4c;
     8'd82:header_char=8'h44;
     8'd83:header_char=8'h3d;
     8'd84:header_char=8'h54;
     8'd85:header_char=8'h58;
     8'd86:header_char=8'h36;
     8'd87:header_char=8'h34;
     8'd88:header_char=8'h2d;
     8'd89:header_char=8'h52;
     8'd90:header_char=8'h58;
     8'd91:header_char=8'h32;
     8'd92:header_char=8'h35;
     8'd93:header_char=8'h36;
     8'd94:header_char=8'h4b;
     8'd95:header_char=8'h2d;
     8'd96:header_char=8'h4e;
     8'd97:header_char=8'h35;
     8'd98:header_char=8'h37;
     8'd99:header_char=8'h42;
     8'd100:header_char=8'h0d;
     8'd101:header_char=8'h0a;
     default:header_char=8'h20;
   endcase
 endfunction
 function automatic [7:0] status_char(input [7:0] n);
   case(n)
     8'd0:status_char=8'h53;
     8'd1:status_char=8'h54;
     8'd2:status_char=8'h41;
     8'd3:status_char=8'h54;
     8'd4:status_char=8'h20;
     8'd5:status_char=8'h6c;
     8'd6:status_char=8'h69;
     8'd7:status_char=8'h6e;
     8'd8:status_char=8'h6b;
     8'd9:status_char=8'h3d;
     8'd10:status_char=(8'h30 + s_link);
     8'd11:status_char=8'h20;
     8'd12:status_char=8'h72;
     8'd13:status_char=8'h78;
     8'd14:status_char=8'h4c;
     8'd15:status_char=8'h6f;
     8'd16:status_char=8'h63;
     8'd17:status_char=8'h6b;
     8'd18:status_char=8'h3d;
     8'd19:status_char=(8'h30 + s_rx_locked);
     8'd20:status_char=8'h20;
     8'd21:status_char=8'h70;
     8'd22:status_char=8'h68;
     8'd23:status_char=8'h61;
     8'd24:status_char=8'h73;
     8'd25:status_char=8'h65;
     8'd26:status_char=8'h3d;
     8'd27:status_char=hex_digit({3'b0,s_phase[4]});
     8'd28:status_char=hex_digit(s_phase[3:0]);
     8'd29:status_char=8'h20;
     8'd30:status_char=8'h72;
     8'd31:status_char=8'h65;
     8'd32:status_char=8'h76;
     8'd33:status_char=8'h65;
     8'd34:status_char=8'h72;
     8'd35:status_char=8'h73;
     8'd36:status_char=8'h65;
     8'd37:status_char=8'h3d;
     8'd38:status_char=(8'h30 + s_reverse);
     8'd39:status_char=8'h20;
     8'd40:status_char=8'h72;
     8'd41:status_char=8'h78;
     8'd42:status_char=8'h42;
     8'd43:status_char=8'h79;
     8'd44:status_char=8'h74;
     8'd45:status_char=8'h65;
     8'd46:status_char=8'h73;
     8'd47:status_char=8'h3d;
     8'd48:status_char=hex_digit(s_rx_bytes[63:60]);
     8'd49:status_char=hex_digit(s_rx_bytes[59:56]);
     8'd50:status_char=hex_digit(s_rx_bytes[55:52]);
     8'd51:status_char=hex_digit(s_rx_bytes[51:48]);
     8'd52:status_char=hex_digit(s_rx_bytes[47:44]);
     8'd53:status_char=hex_digit(s_rx_bytes[43:40]);
     8'd54:status_char=hex_digit(s_rx_bytes[39:36]);
     8'd55:status_char=hex_digit(s_rx_bytes[35:32]);
     8'd56:status_char=hex_digit(s_rx_bytes[31:28]);
     8'd57:status_char=hex_digit(s_rx_bytes[27:24]);
     8'd58:status_char=hex_digit(s_rx_bytes[23:20]);
     8'd59:status_char=hex_digit(s_rx_bytes[19:16]);
     8'd60:status_char=hex_digit(s_rx_bytes[15:12]);
     8'd61:status_char=hex_digit(s_rx_bytes[11:8]);
     8'd62:status_char=hex_digit(s_rx_bytes[7:4]);
     8'd63:status_char=hex_digit(s_rx_bytes[3:0]);
     8'd64:status_char=8'h20;
     8'd65:status_char=8'h74;
     8'd66:status_char=8'h78;
     8'd67:status_char=8'h42;
     8'd68:status_char=8'h79;
     8'd69:status_char=8'h74;
     8'd70:status_char=8'h65;
     8'd71:status_char=8'h73;
     8'd72:status_char=8'h3d;
     8'd73:status_char=hex_digit(s_tx_bytes[63:60]);
     8'd74:status_char=hex_digit(s_tx_bytes[59:56]);
     8'd75:status_char=hex_digit(s_tx_bytes[55:52]);
     8'd76:status_char=hex_digit(s_tx_bytes[51:48]);
     8'd77:status_char=hex_digit(s_tx_bytes[47:44]);
     8'd78:status_char=hex_digit(s_tx_bytes[43:40]);
     8'd79:status_char=hex_digit(s_tx_bytes[39:36]);
     8'd80:status_char=hex_digit(s_tx_bytes[35:32]);
     8'd81:status_char=hex_digit(s_tx_bytes[31:28]);
     8'd82:status_char=hex_digit(s_tx_bytes[27:24]);
     8'd83:status_char=hex_digit(s_tx_bytes[23:20]);
     8'd84:status_char=hex_digit(s_tx_bytes[19:16]);
     8'd85:status_char=hex_digit(s_tx_bytes[15:12]);
     8'd86:status_char=hex_digit(s_tx_bytes[11:8]);
     8'd87:status_char=hex_digit(s_tx_bytes[7:4]);
     8'd88:status_char=hex_digit(s_tx_bytes[3:0]);
     8'd89:status_char=8'h20;
     8'd90:status_char=8'h61;
     8'd91:status_char=8'h70;
     8'd92:status_char=8'h70;
     8'd93:status_char=8'h45;
     8'd94:status_char=8'h72;
     8'd95:status_char=8'h72;
     8'd96:status_char=8'h6f;
     8'd97:status_char=8'h72;
     8'd98:status_char=8'h73;
     8'd99:status_char=8'h3d;
     8'd100:status_char=hex_digit(s_errors[31:28]);
     8'd101:status_char=hex_digit(s_errors[27:24]);
     8'd102:status_char=hex_digit(s_errors[23:20]);
     8'd103:status_char=hex_digit(s_errors[19:16]);
     8'd104:status_char=hex_digit(s_errors[15:12]);
     8'd105:status_char=hex_digit(s_errors[11:8]);
     8'd106:status_char=hex_digit(s_errors[7:4]);
     8'd107:status_char=hex_digit(s_errors[3:0]);
     8'd108:status_char=8'h20;
     8'd109:status_char=8'h70;
     8'd110:status_char=8'h68;
     8'd111:status_char=8'h79;
     8'd112:status_char=8'h46;
     8'd113:status_char=8'h61;
     8'd114:status_char=8'h75;
     8'd115:status_char=8'h6c;
     8'd116:status_char=8'h74;
     8'd117:status_char=8'h3d;
     8'd118:status_char=hex_digit(s_phy_fault[7:4]);
     8'd119:status_char=hex_digit(s_phy_fault[3:0]);
     8'd120:status_char=8'h20;
     8'd121:status_char=8'h6e;
     8'd122:status_char=8'h65;
     8'd123:status_char=8'h74;
     8'd124:status_char=8'h46;
     8'd125:status_char=8'h61;
     8'd126:status_char=8'h75;
     8'd127:status_char=8'h6c;
     8'd128:status_char=8'h74;
     8'd129:status_char=8'h3d;
     8'd130:status_char=(8'h30 + s_net_fault);
     8'd131:status_char=8'h0d;
     8'd132:status_char=8'h0a;
     default:status_char=8'h3f;
   endcase
 endfunction
 function automatic [7:0] diagnostic_char(input [7:0] n);
   case(n)
     8'd0:diagnostic_char=8'h44;
     8'd1:diagnostic_char=8'h49;
     8'd2:diagnostic_char=8'h41;
     8'd3:diagnostic_char=8'h47;
     8'd4:diagnostic_char=8'h20;
     8'd5:diagnostic_char=8'h72;
     8'd6:diagnostic_char=8'h78;
     8'd7:diagnostic_char=8'h3d;
     8'd8:diagnostic_char=hex_digit(s_diagnostics[31:28]);
     8'd9:diagnostic_char=hex_digit(s_diagnostics[27:24]);
     8'd10:diagnostic_char=hex_digit(s_diagnostics[23:20]);
     8'd11:diagnostic_char=hex_digit(s_diagnostics[19:16]);
     8'd12:diagnostic_char=hex_digit(s_diagnostics[15:12]);
     8'd13:diagnostic_char=hex_digit(s_diagnostics[11:8]);
     8'd14:diagnostic_char=hex_digit(s_diagnostics[7:4]);
     8'd15:diagnostic_char=hex_digit(s_diagnostics[3:0]);
     8'd16:diagnostic_char=8'h20;
     8'd17:diagnostic_char=8'h64;
     8'd18:diagnostic_char=8'h72;
     8'd19:diagnostic_char=8'h6f;
     8'd20:diagnostic_char=8'h70;
     8'd21:diagnostic_char=8'h3d;
     8'd22:diagnostic_char=hex_digit(s_diagnostics[63:60]);
     8'd23:diagnostic_char=hex_digit(s_diagnostics[59:56]);
     8'd24:diagnostic_char=hex_digit(s_diagnostics[55:52]);
     8'd25:diagnostic_char=hex_digit(s_diagnostics[51:48]);
     8'd26:diagnostic_char=hex_digit(s_diagnostics[47:44]);
     8'd27:diagnostic_char=hex_digit(s_diagnostics[43:40]);
     8'd28:diagnostic_char=hex_digit(s_diagnostics[39:36]);
     8'd29:diagnostic_char=hex_digit(s_diagnostics[35:32]);
     8'd30:diagnostic_char=8'h20;
     8'd31:diagnostic_char=8'h66;
     8'd32:diagnostic_char=8'h63;
     8'd33:diagnostic_char=8'h73;
     8'd34:diagnostic_char=8'h3d;
     8'd35:diagnostic_char=hex_digit(s_diagnostics[95:92]);
     8'd36:diagnostic_char=hex_digit(s_diagnostics[91:88]);
     8'd37:diagnostic_char=hex_digit(s_diagnostics[87:84]);
     8'd38:diagnostic_char=hex_digit(s_diagnostics[83:80]);
     8'd39:diagnostic_char=hex_digit(s_diagnostics[79:76]);
     8'd40:diagnostic_char=hex_digit(s_diagnostics[75:72]);
     8'd41:diagnostic_char=hex_digit(s_diagnostics[71:68]);
     8'd42:diagnostic_char=hex_digit(s_diagnostics[67:64]);
     8'd43:diagnostic_char=8'h20;
     8'd44:diagnostic_char=8'h77;
     8'd45:diagnostic_char=8'h68;
     8'd46:diagnostic_char=8'h79;
     8'd47:diagnostic_char=8'h3d;
     8'd48:diagnostic_char=hex_digit(s_diagnostics[127:124]);
     8'd49:diagnostic_char=hex_digit(s_diagnostics[123:120]);
     8'd50:diagnostic_char=hex_digit(s_diagnostics[119:116]);
     8'd51:diagnostic_char=hex_digit(s_diagnostics[115:112]);
     8'd52:diagnostic_char=hex_digit(s_diagnostics[111:108]);
     8'd53:diagnostic_char=hex_digit(s_diagnostics[107:104]);
     8'd54:diagnostic_char=hex_digit(s_diagnostics[103:100]);
     8'd55:diagnostic_char=hex_digit(s_diagnostics[99:96]);
     8'd56:diagnostic_char=8'h2c;
     8'd57:diagnostic_char=hex_digit(s_diagnostics[159:156]);
     8'd58:diagnostic_char=hex_digit(s_diagnostics[155:152]);
     8'd59:diagnostic_char=hex_digit(s_diagnostics[151:148]);
     8'd60:diagnostic_char=hex_digit(s_diagnostics[147:144]);
     8'd61:diagnostic_char=hex_digit(s_diagnostics[143:140]);
     8'd62:diagnostic_char=hex_digit(s_diagnostics[139:136]);
     8'd63:diagnostic_char=hex_digit(s_diagnostics[135:132]);
     8'd64:diagnostic_char=hex_digit(s_diagnostics[131:128]);
     8'd65:diagnostic_char=8'h2c;
     8'd66:diagnostic_char=hex_digit(s_diagnostics[191:188]);
     8'd67:diagnostic_char=hex_digit(s_diagnostics[187:184]);
     8'd68:diagnostic_char=hex_digit(s_diagnostics[183:180]);
     8'd69:diagnostic_char=hex_digit(s_diagnostics[179:176]);
     8'd70:diagnostic_char=hex_digit(s_diagnostics[175:172]);
     8'd71:diagnostic_char=hex_digit(s_diagnostics[171:168]);
     8'd72:diagnostic_char=hex_digit(s_diagnostics[167:164]);
     8'd73:diagnostic_char=hex_digit(s_diagnostics[163:160]);
     8'd74:diagnostic_char=8'h2c;
     8'd75:diagnostic_char=hex_digit(s_diagnostics[223:220]);
     8'd76:diagnostic_char=hex_digit(s_diagnostics[219:216]);
     8'd77:diagnostic_char=hex_digit(s_diagnostics[215:212]);
     8'd78:diagnostic_char=hex_digit(s_diagnostics[211:208]);
     8'd79:diagnostic_char=hex_digit(s_diagnostics[207:204]);
     8'd80:diagnostic_char=hex_digit(s_diagnostics[203:200]);
     8'd81:diagnostic_char=hex_digit(s_diagnostics[199:196]);
     8'd82:diagnostic_char=hex_digit(s_diagnostics[195:192]);
     8'd83:diagnostic_char=8'h2c;
     8'd84:diagnostic_char=hex_digit(s_diagnostics[255:252]);
     8'd85:diagnostic_char=hex_digit(s_diagnostics[251:248]);
     8'd86:diagnostic_char=hex_digit(s_diagnostics[247:244]);
     8'd87:diagnostic_char=hex_digit(s_diagnostics[243:240]);
     8'd88:diagnostic_char=hex_digit(s_diagnostics[239:236]);
     8'd89:diagnostic_char=hex_digit(s_diagnostics[235:232]);
     8'd90:diagnostic_char=hex_digit(s_diagnostics[231:228]);
     8'd91:diagnostic_char=hex_digit(s_diagnostics[227:224]);
     8'd92:diagnostic_char=8'h2c;
     8'd93:diagnostic_char=hex_digit(s_diagnostics[287:284]);
     8'd94:diagnostic_char=hex_digit(s_diagnostics[283:280]);
     8'd95:diagnostic_char=hex_digit(s_diagnostics[279:276]);
     8'd96:diagnostic_char=hex_digit(s_diagnostics[275:272]);
     8'd97:diagnostic_char=hex_digit(s_diagnostics[271:268]);
     8'd98:diagnostic_char=hex_digit(s_diagnostics[267:264]);
     8'd99:diagnostic_char=hex_digit(s_diagnostics[263:260]);
     8'd100:diagnostic_char=hex_digit(s_diagnostics[259:256]);
     8'd101:diagnostic_char=8'h2c;
     8'd102:diagnostic_char=hex_digit(s_diagnostics[319:316]);
     8'd103:diagnostic_char=hex_digit(s_diagnostics[315:312]);
     8'd104:diagnostic_char=hex_digit(s_diagnostics[311:308]);
     8'd105:diagnostic_char=hex_digit(s_diagnostics[307:304]);
     8'd106:diagnostic_char=hex_digit(s_diagnostics[303:300]);
     8'd107:diagnostic_char=hex_digit(s_diagnostics[299:296]);
     8'd108:diagnostic_char=hex_digit(s_diagnostics[295:292]);
     8'd109:diagnostic_char=hex_digit(s_diagnostics[291:288]);
     8'd110:diagnostic_char=8'h2c;
     8'd111:diagnostic_char=hex_digit(s_diagnostics[351:348]);
     8'd112:diagnostic_char=hex_digit(s_diagnostics[347:344]);
     8'd113:diagnostic_char=hex_digit(s_diagnostics[343:340]);
     8'd114:diagnostic_char=hex_digit(s_diagnostics[339:336]);
     8'd115:diagnostic_char=hex_digit(s_diagnostics[335:332]);
     8'd116:diagnostic_char=hex_digit(s_diagnostics[331:328]);
     8'd117:diagnostic_char=hex_digit(s_diagnostics[327:324]);
     8'd118:diagnostic_char=hex_digit(s_diagnostics[323:320]);
     8'd119:diagnostic_char=8'h20;
     8'd120:diagnostic_char=8'h73;
     8'd121:diagnostic_char=8'h74;
     8'd122:diagnostic_char=8'h61;
     8'd123:diagnostic_char=8'h6c;
     8'd124:diagnostic_char=8'h6c;
     8'd125:diagnostic_char=8'h3d;
     8'd126:diagnostic_char=hex_digit(s_diagnostics[383:380]);
     8'd127:diagnostic_char=hex_digit(s_diagnostics[379:376]);
     8'd128:diagnostic_char=hex_digit(s_diagnostics[375:372]);
     8'd129:diagnostic_char=hex_digit(s_diagnostics[371:368]);
     8'd130:diagnostic_char=hex_digit(s_diagnostics[367:364]);
     8'd131:diagnostic_char=hex_digit(s_diagnostics[363:360]);
     8'd132:diagnostic_char=hex_digit(s_diagnostics[359:356]);
     8'd133:diagnostic_char=hex_digit(s_diagnostics[355:352]);
     8'd134:diagnostic_char=8'h20;
     8'd135:diagnostic_char=8'h6d;
     8'd136:diagnostic_char=8'h61;
     8'd137:diagnostic_char=8'h78;
     8'd138:diagnostic_char=8'h3d;
     8'd139:diagnostic_char=hex_digit(s_diagnostics[415:412]);
     8'd140:diagnostic_char=hex_digit(s_diagnostics[411:408]);
     8'd141:diagnostic_char=hex_digit(s_diagnostics[407:404]);
     8'd142:diagnostic_char=hex_digit(s_diagnostics[403:400]);
     8'd143:diagnostic_char=hex_digit(s_diagnostics[399:396]);
     8'd144:diagnostic_char=hex_digit(s_diagnostics[395:392]);
     8'd145:diagnostic_char=hex_digit(s_diagnostics[391:388]);
     8'd146:diagnostic_char=hex_digit(s_diagnostics[387:384]);
     8'd147:diagnostic_char=8'h20;
     8'd148:diagnostic_char=8'h73;
     8'd149:diagnostic_char=8'h72;
     8'd150:diagnostic_char=8'h3d;
     8'd151:diagnostic_char=hex_digit(s_diagnostics[447:444]);
     8'd152:diagnostic_char=hex_digit(s_diagnostics[443:440]);
     8'd153:diagnostic_char=hex_digit(s_diagnostics[439:436]);
     8'd154:diagnostic_char=hex_digit(s_diagnostics[435:432]);
     8'd155:diagnostic_char=hex_digit(s_diagnostics[431:428]);
     8'd156:diagnostic_char=hex_digit(s_diagnostics[427:424]);
     8'd157:diagnostic_char=hex_digit(s_diagnostics[423:420]);
     8'd158:diagnostic_char=hex_digit(s_diagnostics[419:416]);
     8'd159:diagnostic_char=8'h20;
     8'd160:diagnostic_char=8'h73;
     8'd161:diagnostic_char=8'h62;
     8'd162:diagnostic_char=8'h3d;
     8'd163:diagnostic_char=hex_digit(s_diagnostics[479:476]);
     8'd164:diagnostic_char=hex_digit(s_diagnostics[475:472]);
     8'd165:diagnostic_char=hex_digit(s_diagnostics[471:468]);
     8'd166:diagnostic_char=hex_digit(s_diagnostics[467:464]);
     8'd167:diagnostic_char=hex_digit(s_diagnostics[463:460]);
     8'd168:diagnostic_char=hex_digit(s_diagnostics[459:456]);
     8'd169:diagnostic_char=hex_digit(s_diagnostics[455:452]);
     8'd170:diagnostic_char=hex_digit(s_diagnostics[451:448]);
     8'd171:diagnostic_char=8'h20;
     8'd172:diagnostic_char=8'h73;
     8'd173:diagnostic_char=8'h74;
     8'd174:diagnostic_char=8'h3d;
     8'd175:diagnostic_char=hex_digit(s_diagnostics[511:508]);
     8'd176:diagnostic_char=hex_digit(s_diagnostics[507:504]);
     8'd177:diagnostic_char=hex_digit(s_diagnostics[503:500]);
     8'd178:diagnostic_char=hex_digit(s_diagnostics[499:496]);
     8'd179:diagnostic_char=hex_digit(s_diagnostics[495:492]);
     8'd180:diagnostic_char=hex_digit(s_diagnostics[491:488]);
     8'd181:diagnostic_char=hex_digit(s_diagnostics[487:484]);
     8'd182:diagnostic_char=hex_digit(s_diagnostics[483:480]);
     8'd183:diagnostic_char=8'h20;
     8'd184:diagnostic_char=8'h74;
     8'd185:diagnostic_char=8'h78;
     8'd186:diagnostic_char=8'h3d;
     8'd187:diagnostic_char=hex_digit(s_diagnostics[543:540]);
     8'd188:diagnostic_char=hex_digit(s_diagnostics[539:536]);
     8'd189:diagnostic_char=hex_digit(s_diagnostics[535:532]);
     8'd190:diagnostic_char=hex_digit(s_diagnostics[531:528]);
     8'd191:diagnostic_char=hex_digit(s_diagnostics[527:524]);
     8'd192:diagnostic_char=hex_digit(s_diagnostics[523:520]);
     8'd193:diagnostic_char=hex_digit(s_diagnostics[519:516]);
     8'd194:diagnostic_char=hex_digit(s_diagnostics[515:512]);
     8'd195:diagnostic_char=8'h20;
     8'd196:diagnostic_char=8'h72;
     8'd197:diagnostic_char=8'h65;
     8'd198:diagnostic_char=8'h6a;
     8'd199:diagnostic_char=8'h3d;
     8'd200:diagnostic_char=hex_digit(s_diagnostics[575:572]);
     8'd201:diagnostic_char=hex_digit(s_diagnostics[571:568]);
     8'd202:diagnostic_char=hex_digit(s_diagnostics[567:564]);
     8'd203:diagnostic_char=hex_digit(s_diagnostics[563:560]);
     8'd204:diagnostic_char=hex_digit(s_diagnostics[559:556]);
     8'd205:diagnostic_char=hex_digit(s_diagnostics[555:552]);
     8'd206:diagnostic_char=hex_digit(s_diagnostics[551:548]);
     8'd207:diagnostic_char=hex_digit(s_diagnostics[547:544]);
     8'd208:diagnostic_char=8'h20;
     8'd209:diagnostic_char=8'h69;
     8'd210:diagnostic_char=8'h62;
     8'd211:diagnostic_char=8'h3d;
     8'd212:diagnostic_char=hex_digit(s_diagnostics[607:604]);
     8'd213:diagnostic_char=hex_digit(s_diagnostics[603:600]);
     8'd214:diagnostic_char=hex_digit(s_diagnostics[599:596]);
     8'd215:diagnostic_char=hex_digit(s_diagnostics[595:592]);
     8'd216:diagnostic_char=hex_digit(s_diagnostics[591:588]);
     8'd217:diagnostic_char=hex_digit(s_diagnostics[587:584]);
     8'd218:diagnostic_char=hex_digit(s_diagnostics[583:580]);
     8'd219:diagnostic_char=hex_digit(s_diagnostics[579:576]);
     8'd220:diagnostic_char=8'h0d;
     8'd221:diagnostic_char=8'h0a;
     default:diagnostic_char=8'h3f;
   endcase
 endfunction
 reg busy;
 reg [9:0] shift;
 reg [3:0] bit_index;
 reg [BAUD_WIDTH-1:0] baud_count;
 wire offer=(state==HEADER || state==STATUS || state==DIAGNOSTIC);
 wire accept=offer && !busy;
 wire [7:0] character=(state==HEADER)?header_char(character_index):
                      (state==STATUS)?status_char(character_index):diagnostic_char(character_index);
 // LSB-first start/data/stop; exactly BAUD_DIV clocks for each of ten bits.
 // shift stays all ones after the stop bit, including reset and idle.
 assign uart_tx=shift[0];
 always @(posedge clock or posedge reset)begin
   if(reset)begin busy<=0;shift<=10'h3ff;bit_index<=0;baud_count<=0;end
   else if(accept)begin
     busy<=1;shift<={1'b1,character,1'b0};bit_index<=0;baud_count<=0;
   end else if(busy)begin
     if(baud_count==BAUD_DIV-1)begin
       baud_count<=0;shift<={1'b1,shift[9:1]};
       if(bit_index==9)busy<=0;
       else bit_index<=bit_index+1'b1;
     end else baud_count<=baud_count+1'b1;
   end
 end
 wire take_snapshot=(state==HEADER && accept && character_index==HEADER_BYTES-1) ||
                    (state==IDLE && (pending || heartbeat_due || observed!=previous));
 wire take_diagnostics=(state==STATUS && accept && character_index==STATUS_BYTES-1 &&
                        (diagnostic_pending || heartbeat_due)) ||
                       (state==IDLE && !take_snapshot && diagnostic_pending);
 always @(posedge clock or posedge reset)begin
   if(reset)begin
     previous<=0;pending<=1;diagnostic_pending<=1;state<=HEADER;character_index<=0;
     s_link<=0;s_rx_locked<=0;s_phase<=0;s_reverse<=0;
     s_rx_bytes<=0;s_tx_bytes<=0;s_errors<=0;s_phy_fault<=0;s_net_fault<=0;
     s_diagnostics<=0;
   end else begin
     previous<=observed;
     if(heartbeat_due || observed!=previous)pending<=1;
     // Counter changes deliberately do not trigger a UART line for every packet.
     if(heartbeat_due)diagnostic_pending<=1;
     if(accept)begin
       if(state==HEADER && character_index==HEADER_BYTES-1)begin
         state<=STATUS;character_index<=0;
       end else if((state==STATUS && character_index==STATUS_BYTES-1) ||
                   (state==DIAGNOSTIC && character_index==DIAGNOSTIC_BYTES-1))begin
         state<=IDLE;character_index<=0;
       end else character_index<=character_index+1'b1;
     end
     if(take_snapshot)begin
       s_link<=link;s_rx_locked<=rx_lock_sync;s_phase<=phase;s_reverse<=reverse;
       s_rx_bytes<=rx_bytes;s_tx_bytes<=tx_bytes;s_errors<=app_errors;
       s_phy_fault<=phy_fault;s_net_fault<=net_fault;
       state<=STATUS;character_index<=0;pending<=0;
     end
     if(take_diagnostics)begin
       s_diagnostics<=diagnostics;diagnostic_pending<=0;
       state<=DIAGNOSTIC;character_index<=0;
     end
   end
 end
endmodule
