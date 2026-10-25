// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// --------------------------------------------------------------------------------
// Tool Version: Vivado v.2025.2 (win64) Build 6299465 Fri Nov 14 19:35:11 GMT 2025
// Date        : Sun Oct 25 16:21:53 2026
// Host        : DESKTOP-HUJSC9H running 64-bit major release  (build 9200)
// Command     : write_verilog -force -mode funcsim E:/VM/Share/Valence-rtl/memory-lab-20261025-selective/ultra_netlist.v
// Design      : UltraStoreProbe
// Purpose     : This verilog netlist is a functional simulation representation of the design and should not be modified
//               or synthesized. This netlist cannot be used for SDF annotated simulation.
// Device      : xczu15eg-ffvb1156-2-i
// --------------------------------------------------------------------------------
`timescale 1 ps / 1 ps

(* NotValidForBitStream *)
module UltraStoreProbe
   (clock,
    reset,
    io_wa,
    io_wd,
    io_we,
    io_ra,
    io_re,
    io_rd);
  input clock;
  input reset;
  input [9:0]io_wa;
  input [127:0]io_wd;
  input io_we;
  input [9:0]io_ra;
  input io_re;
  output [127:0]io_rd;

  wire clock;
  wire clock_IBUF;
  wire clock_IBUF_BUFG;
  wire [9:0]io_ra;
  wire [9:0]io_ra_IBUF;
  wire [127:0]io_rd;
  wire [127:0]io_rd_OBUF;
  wire [9:0]io_wa;
  wire [9:0]io_wa_IBUF;
  wire [127:0]io_wd;
  wire [127:0]io_wd_IBUF;
  wire io_we;
  wire io_we_IBUF;

  (* XILINX_LEGACY_PRIM = "BUFG" *) 
  (* XILINX_TRANSFORM_PINMAP = "VCC:CE" *) 
  BUFGCE #(
    .CE_TYPE("ASYNC"),
    .SIM_DEVICE("ULTRASCALE_PLUS")) 
    clock_IBUF_BUFG_inst
       (.CE(1'b1),
        .I(clock_IBUF),
        .O(clock_IBUF_BUFG));
  IBUF clock_IBUF_inst
       (.I(clock),
        .O(clock_IBUF));
  IBUF \io_ra_IBUF[0]_inst 
       (.I(io_ra[0]),
        .O(io_ra_IBUF[0]));
  IBUF \io_ra_IBUF[1]_inst 
       (.I(io_ra[1]),
        .O(io_ra_IBUF[1]));
  IBUF \io_ra_IBUF[2]_inst 
       (.I(io_ra[2]),
        .O(io_ra_IBUF[2]));
  IBUF \io_ra_IBUF[3]_inst 
       (.I(io_ra[3]),
        .O(io_ra_IBUF[3]));
  IBUF \io_ra_IBUF[4]_inst 
       (.I(io_ra[4]),
        .O(io_ra_IBUF[4]));
  IBUF \io_ra_IBUF[5]_inst 
       (.I(io_ra[5]),
        .O(io_ra_IBUF[5]));
  IBUF \io_ra_IBUF[6]_inst 
       (.I(io_ra[6]),
        .O(io_ra_IBUF[6]));
  IBUF \io_ra_IBUF[7]_inst 
       (.I(io_ra[7]),
        .O(io_ra_IBUF[7]));
  IBUF \io_ra_IBUF[8]_inst 
       (.I(io_ra[8]),
        .O(io_ra_IBUF[8]));
  IBUF \io_ra_IBUF[9]_inst 
       (.I(io_ra[9]),
        .O(io_ra_IBUF[9]));
  OBUF \io_rd_OBUF[0]_inst 
       (.I(io_rd_OBUF[0]),
        .O(io_rd[0]));
  OBUF \io_rd_OBUF[100]_inst 
       (.I(io_rd_OBUF[100]),
        .O(io_rd[100]));
  OBUF \io_rd_OBUF[101]_inst 
       (.I(io_rd_OBUF[101]),
        .O(io_rd[101]));
  OBUF \io_rd_OBUF[102]_inst 
       (.I(io_rd_OBUF[102]),
        .O(io_rd[102]));
  OBUF \io_rd_OBUF[103]_inst 
       (.I(io_rd_OBUF[103]),
        .O(io_rd[103]));
  OBUF \io_rd_OBUF[104]_inst 
       (.I(io_rd_OBUF[104]),
        .O(io_rd[104]));
  OBUF \io_rd_OBUF[105]_inst 
       (.I(io_rd_OBUF[105]),
        .O(io_rd[105]));
  OBUF \io_rd_OBUF[106]_inst 
       (.I(io_rd_OBUF[106]),
        .O(io_rd[106]));
  OBUF \io_rd_OBUF[107]_inst 
       (.I(io_rd_OBUF[107]),
        .O(io_rd[107]));
  OBUF \io_rd_OBUF[108]_inst 
       (.I(io_rd_OBUF[108]),
        .O(io_rd[108]));
  OBUF \io_rd_OBUF[109]_inst 
       (.I(io_rd_OBUF[109]),
        .O(io_rd[109]));
  OBUF \io_rd_OBUF[10]_inst 
       (.I(io_rd_OBUF[10]),
        .O(io_rd[10]));
  OBUF \io_rd_OBUF[110]_inst 
       (.I(io_rd_OBUF[110]),
        .O(io_rd[110]));
  OBUF \io_rd_OBUF[111]_inst 
       (.I(io_rd_OBUF[111]),
        .O(io_rd[111]));
  OBUF \io_rd_OBUF[112]_inst 
       (.I(io_rd_OBUF[112]),
        .O(io_rd[112]));
  OBUF \io_rd_OBUF[113]_inst 
       (.I(io_rd_OBUF[113]),
        .O(io_rd[113]));
  OBUF \io_rd_OBUF[114]_inst 
       (.I(io_rd_OBUF[114]),
        .O(io_rd[114]));
  OBUF \io_rd_OBUF[115]_inst 
       (.I(io_rd_OBUF[115]),
        .O(io_rd[115]));
  OBUF \io_rd_OBUF[116]_inst 
       (.I(io_rd_OBUF[116]),
        .O(io_rd[116]));
  OBUF \io_rd_OBUF[117]_inst 
       (.I(io_rd_OBUF[117]),
        .O(io_rd[117]));
  OBUF \io_rd_OBUF[118]_inst 
       (.I(io_rd_OBUF[118]),
        .O(io_rd[118]));
  OBUF \io_rd_OBUF[119]_inst 
       (.I(io_rd_OBUF[119]),
        .O(io_rd[119]));
  OBUF \io_rd_OBUF[11]_inst 
       (.I(io_rd_OBUF[11]),
        .O(io_rd[11]));
  OBUF \io_rd_OBUF[120]_inst 
       (.I(io_rd_OBUF[120]),
        .O(io_rd[120]));
  OBUF \io_rd_OBUF[121]_inst 
       (.I(io_rd_OBUF[121]),
        .O(io_rd[121]));
  OBUF \io_rd_OBUF[122]_inst 
       (.I(io_rd_OBUF[122]),
        .O(io_rd[122]));
  OBUF \io_rd_OBUF[123]_inst 
       (.I(io_rd_OBUF[123]),
        .O(io_rd[123]));
  OBUF \io_rd_OBUF[124]_inst 
       (.I(io_rd_OBUF[124]),
        .O(io_rd[124]));
  OBUF \io_rd_OBUF[125]_inst 
       (.I(io_rd_OBUF[125]),
        .O(io_rd[125]));
  OBUF \io_rd_OBUF[126]_inst 
       (.I(io_rd_OBUF[126]),
        .O(io_rd[126]));
  OBUF \io_rd_OBUF[127]_inst 
       (.I(io_rd_OBUF[127]),
        .O(io_rd[127]));
  OBUF \io_rd_OBUF[12]_inst 
       (.I(io_rd_OBUF[12]),
        .O(io_rd[12]));
  OBUF \io_rd_OBUF[13]_inst 
       (.I(io_rd_OBUF[13]),
        .O(io_rd[13]));
  OBUF \io_rd_OBUF[14]_inst 
       (.I(io_rd_OBUF[14]),
        .O(io_rd[14]));
  OBUF \io_rd_OBUF[15]_inst 
       (.I(io_rd_OBUF[15]),
        .O(io_rd[15]));
  OBUF \io_rd_OBUF[16]_inst 
       (.I(io_rd_OBUF[16]),
        .O(io_rd[16]));
  OBUF \io_rd_OBUF[17]_inst 
       (.I(io_rd_OBUF[17]),
        .O(io_rd[17]));
  OBUF \io_rd_OBUF[18]_inst 
       (.I(io_rd_OBUF[18]),
        .O(io_rd[18]));
  OBUF \io_rd_OBUF[19]_inst 
       (.I(io_rd_OBUF[19]),
        .O(io_rd[19]));
  OBUF \io_rd_OBUF[1]_inst 
       (.I(io_rd_OBUF[1]),
        .O(io_rd[1]));
  OBUF \io_rd_OBUF[20]_inst 
       (.I(io_rd_OBUF[20]),
        .O(io_rd[20]));
  OBUF \io_rd_OBUF[21]_inst 
       (.I(io_rd_OBUF[21]),
        .O(io_rd[21]));
  OBUF \io_rd_OBUF[22]_inst 
       (.I(io_rd_OBUF[22]),
        .O(io_rd[22]));
  OBUF \io_rd_OBUF[23]_inst 
       (.I(io_rd_OBUF[23]),
        .O(io_rd[23]));
  OBUF \io_rd_OBUF[24]_inst 
       (.I(io_rd_OBUF[24]),
        .O(io_rd[24]));
  OBUF \io_rd_OBUF[25]_inst 
       (.I(io_rd_OBUF[25]),
        .O(io_rd[25]));
  OBUF \io_rd_OBUF[26]_inst 
       (.I(io_rd_OBUF[26]),
        .O(io_rd[26]));
  OBUF \io_rd_OBUF[27]_inst 
       (.I(io_rd_OBUF[27]),
        .O(io_rd[27]));
  OBUF \io_rd_OBUF[28]_inst 
       (.I(io_rd_OBUF[28]),
        .O(io_rd[28]));
  OBUF \io_rd_OBUF[29]_inst 
       (.I(io_rd_OBUF[29]),
        .O(io_rd[29]));
  OBUF \io_rd_OBUF[2]_inst 
       (.I(io_rd_OBUF[2]),
        .O(io_rd[2]));
  OBUF \io_rd_OBUF[30]_inst 
       (.I(io_rd_OBUF[30]),
        .O(io_rd[30]));
  OBUF \io_rd_OBUF[31]_inst 
       (.I(io_rd_OBUF[31]),
        .O(io_rd[31]));
  OBUF \io_rd_OBUF[32]_inst 
       (.I(io_rd_OBUF[32]),
        .O(io_rd[32]));
  OBUF \io_rd_OBUF[33]_inst 
       (.I(io_rd_OBUF[33]),
        .O(io_rd[33]));
  OBUF \io_rd_OBUF[34]_inst 
       (.I(io_rd_OBUF[34]),
        .O(io_rd[34]));
  OBUF \io_rd_OBUF[35]_inst 
       (.I(io_rd_OBUF[35]),
        .O(io_rd[35]));
  OBUF \io_rd_OBUF[36]_inst 
       (.I(io_rd_OBUF[36]),
        .O(io_rd[36]));
  OBUF \io_rd_OBUF[37]_inst 
       (.I(io_rd_OBUF[37]),
        .O(io_rd[37]));
  OBUF \io_rd_OBUF[38]_inst 
       (.I(io_rd_OBUF[38]),
        .O(io_rd[38]));
  OBUF \io_rd_OBUF[39]_inst 
       (.I(io_rd_OBUF[39]),
        .O(io_rd[39]));
  OBUF \io_rd_OBUF[3]_inst 
       (.I(io_rd_OBUF[3]),
        .O(io_rd[3]));
  OBUF \io_rd_OBUF[40]_inst 
       (.I(io_rd_OBUF[40]),
        .O(io_rd[40]));
  OBUF \io_rd_OBUF[41]_inst 
       (.I(io_rd_OBUF[41]),
        .O(io_rd[41]));
  OBUF \io_rd_OBUF[42]_inst 
       (.I(io_rd_OBUF[42]),
        .O(io_rd[42]));
  OBUF \io_rd_OBUF[43]_inst 
       (.I(io_rd_OBUF[43]),
        .O(io_rd[43]));
  OBUF \io_rd_OBUF[44]_inst 
       (.I(io_rd_OBUF[44]),
        .O(io_rd[44]));
  OBUF \io_rd_OBUF[45]_inst 
       (.I(io_rd_OBUF[45]),
        .O(io_rd[45]));
  OBUF \io_rd_OBUF[46]_inst 
       (.I(io_rd_OBUF[46]),
        .O(io_rd[46]));
  OBUF \io_rd_OBUF[47]_inst 
       (.I(io_rd_OBUF[47]),
        .O(io_rd[47]));
  OBUF \io_rd_OBUF[48]_inst 
       (.I(io_rd_OBUF[48]),
        .O(io_rd[48]));
  OBUF \io_rd_OBUF[49]_inst 
       (.I(io_rd_OBUF[49]),
        .O(io_rd[49]));
  OBUF \io_rd_OBUF[4]_inst 
       (.I(io_rd_OBUF[4]),
        .O(io_rd[4]));
  OBUF \io_rd_OBUF[50]_inst 
       (.I(io_rd_OBUF[50]),
        .O(io_rd[50]));
  OBUF \io_rd_OBUF[51]_inst 
       (.I(io_rd_OBUF[51]),
        .O(io_rd[51]));
  OBUF \io_rd_OBUF[52]_inst 
       (.I(io_rd_OBUF[52]),
        .O(io_rd[52]));
  OBUF \io_rd_OBUF[53]_inst 
       (.I(io_rd_OBUF[53]),
        .O(io_rd[53]));
  OBUF \io_rd_OBUF[54]_inst 
       (.I(io_rd_OBUF[54]),
        .O(io_rd[54]));
  OBUF \io_rd_OBUF[55]_inst 
       (.I(io_rd_OBUF[55]),
        .O(io_rd[55]));
  OBUF \io_rd_OBUF[56]_inst 
       (.I(io_rd_OBUF[56]),
        .O(io_rd[56]));
  OBUF \io_rd_OBUF[57]_inst 
       (.I(io_rd_OBUF[57]),
        .O(io_rd[57]));
  OBUF \io_rd_OBUF[58]_inst 
       (.I(io_rd_OBUF[58]),
        .O(io_rd[58]));
  OBUF \io_rd_OBUF[59]_inst 
       (.I(io_rd_OBUF[59]),
        .O(io_rd[59]));
  OBUF \io_rd_OBUF[5]_inst 
       (.I(io_rd_OBUF[5]),
        .O(io_rd[5]));
  OBUF \io_rd_OBUF[60]_inst 
       (.I(io_rd_OBUF[60]),
        .O(io_rd[60]));
  OBUF \io_rd_OBUF[61]_inst 
       (.I(io_rd_OBUF[61]),
        .O(io_rd[61]));
  OBUF \io_rd_OBUF[62]_inst 
       (.I(io_rd_OBUF[62]),
        .O(io_rd[62]));
  OBUF \io_rd_OBUF[63]_inst 
       (.I(io_rd_OBUF[63]),
        .O(io_rd[63]));
  OBUF \io_rd_OBUF[64]_inst 
       (.I(io_rd_OBUF[64]),
        .O(io_rd[64]));
  OBUF \io_rd_OBUF[65]_inst 
       (.I(io_rd_OBUF[65]),
        .O(io_rd[65]));
  OBUF \io_rd_OBUF[66]_inst 
       (.I(io_rd_OBUF[66]),
        .O(io_rd[66]));
  OBUF \io_rd_OBUF[67]_inst 
       (.I(io_rd_OBUF[67]),
        .O(io_rd[67]));
  OBUF \io_rd_OBUF[68]_inst 
       (.I(io_rd_OBUF[68]),
        .O(io_rd[68]));
  OBUF \io_rd_OBUF[69]_inst 
       (.I(io_rd_OBUF[69]),
        .O(io_rd[69]));
  OBUF \io_rd_OBUF[6]_inst 
       (.I(io_rd_OBUF[6]),
        .O(io_rd[6]));
  OBUF \io_rd_OBUF[70]_inst 
       (.I(io_rd_OBUF[70]),
        .O(io_rd[70]));
  OBUF \io_rd_OBUF[71]_inst 
       (.I(io_rd_OBUF[71]),
        .O(io_rd[71]));
  OBUF \io_rd_OBUF[72]_inst 
       (.I(io_rd_OBUF[72]),
        .O(io_rd[72]));
  OBUF \io_rd_OBUF[73]_inst 
       (.I(io_rd_OBUF[73]),
        .O(io_rd[73]));
  OBUF \io_rd_OBUF[74]_inst 
       (.I(io_rd_OBUF[74]),
        .O(io_rd[74]));
  OBUF \io_rd_OBUF[75]_inst 
       (.I(io_rd_OBUF[75]),
        .O(io_rd[75]));
  OBUF \io_rd_OBUF[76]_inst 
       (.I(io_rd_OBUF[76]),
        .O(io_rd[76]));
  OBUF \io_rd_OBUF[77]_inst 
       (.I(io_rd_OBUF[77]),
        .O(io_rd[77]));
  OBUF \io_rd_OBUF[78]_inst 
       (.I(io_rd_OBUF[78]),
        .O(io_rd[78]));
  OBUF \io_rd_OBUF[79]_inst 
       (.I(io_rd_OBUF[79]),
        .O(io_rd[79]));
  OBUF \io_rd_OBUF[7]_inst 
       (.I(io_rd_OBUF[7]),
        .O(io_rd[7]));
  OBUF \io_rd_OBUF[80]_inst 
       (.I(io_rd_OBUF[80]),
        .O(io_rd[80]));
  OBUF \io_rd_OBUF[81]_inst 
       (.I(io_rd_OBUF[81]),
        .O(io_rd[81]));
  OBUF \io_rd_OBUF[82]_inst 
       (.I(io_rd_OBUF[82]),
        .O(io_rd[82]));
  OBUF \io_rd_OBUF[83]_inst 
       (.I(io_rd_OBUF[83]),
        .O(io_rd[83]));
  OBUF \io_rd_OBUF[84]_inst 
       (.I(io_rd_OBUF[84]),
        .O(io_rd[84]));
  OBUF \io_rd_OBUF[85]_inst 
       (.I(io_rd_OBUF[85]),
        .O(io_rd[85]));
  OBUF \io_rd_OBUF[86]_inst 
       (.I(io_rd_OBUF[86]),
        .O(io_rd[86]));
  OBUF \io_rd_OBUF[87]_inst 
       (.I(io_rd_OBUF[87]),
        .O(io_rd[87]));
  OBUF \io_rd_OBUF[88]_inst 
       (.I(io_rd_OBUF[88]),
        .O(io_rd[88]));
  OBUF \io_rd_OBUF[89]_inst 
       (.I(io_rd_OBUF[89]),
        .O(io_rd[89]));
  OBUF \io_rd_OBUF[8]_inst 
       (.I(io_rd_OBUF[8]),
        .O(io_rd[8]));
  OBUF \io_rd_OBUF[90]_inst 
       (.I(io_rd_OBUF[90]),
        .O(io_rd[90]));
  OBUF \io_rd_OBUF[91]_inst 
       (.I(io_rd_OBUF[91]),
        .O(io_rd[91]));
  OBUF \io_rd_OBUF[92]_inst 
       (.I(io_rd_OBUF[92]),
        .O(io_rd[92]));
  OBUF \io_rd_OBUF[93]_inst 
       (.I(io_rd_OBUF[93]),
        .O(io_rd[93]));
  OBUF \io_rd_OBUF[94]_inst 
       (.I(io_rd_OBUF[94]),
        .O(io_rd[94]));
  OBUF \io_rd_OBUF[95]_inst 
       (.I(io_rd_OBUF[95]),
        .O(io_rd[95]));
  OBUF \io_rd_OBUF[96]_inst 
       (.I(io_rd_OBUF[96]),
        .O(io_rd[96]));
  OBUF \io_rd_OBUF[97]_inst 
       (.I(io_rd_OBUF[97]),
        .O(io_rd[97]));
  OBUF \io_rd_OBUF[98]_inst 
       (.I(io_rd_OBUF[98]),
        .O(io_rd[98]));
  OBUF \io_rd_OBUF[99]_inst 
       (.I(io_rd_OBUF[99]),
        .O(io_rd[99]));
  OBUF \io_rd_OBUF[9]_inst 
       (.I(io_rd_OBUF[9]),
        .O(io_rd[9]));
  IBUF \io_wa_IBUF[0]_inst 
       (.I(io_wa[0]),
        .O(io_wa_IBUF[0]));
  IBUF \io_wa_IBUF[1]_inst 
       (.I(io_wa[1]),
        .O(io_wa_IBUF[1]));
  IBUF \io_wa_IBUF[2]_inst 
       (.I(io_wa[2]),
        .O(io_wa_IBUF[2]));
  IBUF \io_wa_IBUF[3]_inst 
       (.I(io_wa[3]),
        .O(io_wa_IBUF[3]));
  IBUF \io_wa_IBUF[4]_inst 
       (.I(io_wa[4]),
        .O(io_wa_IBUF[4]));
  IBUF \io_wa_IBUF[5]_inst 
       (.I(io_wa[5]),
        .O(io_wa_IBUF[5]));
  IBUF \io_wa_IBUF[6]_inst 
       (.I(io_wa[6]),
        .O(io_wa_IBUF[6]));
  IBUF \io_wa_IBUF[7]_inst 
       (.I(io_wa[7]),
        .O(io_wa_IBUF[7]));
  IBUF \io_wa_IBUF[8]_inst 
       (.I(io_wa[8]),
        .O(io_wa_IBUF[8]));
  IBUF \io_wa_IBUF[9]_inst 
       (.I(io_wa[9]),
        .O(io_wa_IBUF[9]));
  IBUF \io_wd_IBUF[0]_inst 
       (.I(io_wd[0]),
        .O(io_wd_IBUF[0]));
  IBUF \io_wd_IBUF[100]_inst 
       (.I(io_wd[100]),
        .O(io_wd_IBUF[100]));
  IBUF \io_wd_IBUF[101]_inst 
       (.I(io_wd[101]),
        .O(io_wd_IBUF[101]));
  IBUF \io_wd_IBUF[102]_inst 
       (.I(io_wd[102]),
        .O(io_wd_IBUF[102]));
  IBUF \io_wd_IBUF[103]_inst 
       (.I(io_wd[103]),
        .O(io_wd_IBUF[103]));
  IBUF \io_wd_IBUF[104]_inst 
       (.I(io_wd[104]),
        .O(io_wd_IBUF[104]));
  IBUF \io_wd_IBUF[105]_inst 
       (.I(io_wd[105]),
        .O(io_wd_IBUF[105]));
  IBUF \io_wd_IBUF[106]_inst 
       (.I(io_wd[106]),
        .O(io_wd_IBUF[106]));
  IBUF \io_wd_IBUF[107]_inst 
       (.I(io_wd[107]),
        .O(io_wd_IBUF[107]));
  IBUF \io_wd_IBUF[108]_inst 
       (.I(io_wd[108]),
        .O(io_wd_IBUF[108]));
  IBUF \io_wd_IBUF[109]_inst 
       (.I(io_wd[109]),
        .O(io_wd_IBUF[109]));
  IBUF \io_wd_IBUF[10]_inst 
       (.I(io_wd[10]),
        .O(io_wd_IBUF[10]));
  IBUF \io_wd_IBUF[110]_inst 
       (.I(io_wd[110]),
        .O(io_wd_IBUF[110]));
  IBUF \io_wd_IBUF[111]_inst 
       (.I(io_wd[111]),
        .O(io_wd_IBUF[111]));
  IBUF \io_wd_IBUF[112]_inst 
       (.I(io_wd[112]),
        .O(io_wd_IBUF[112]));
  IBUF \io_wd_IBUF[113]_inst 
       (.I(io_wd[113]),
        .O(io_wd_IBUF[113]));
  IBUF \io_wd_IBUF[114]_inst 
       (.I(io_wd[114]),
        .O(io_wd_IBUF[114]));
  IBUF \io_wd_IBUF[115]_inst 
       (.I(io_wd[115]),
        .O(io_wd_IBUF[115]));
  IBUF \io_wd_IBUF[116]_inst 
       (.I(io_wd[116]),
        .O(io_wd_IBUF[116]));
  IBUF \io_wd_IBUF[117]_inst 
       (.I(io_wd[117]),
        .O(io_wd_IBUF[117]));
  IBUF \io_wd_IBUF[118]_inst 
       (.I(io_wd[118]),
        .O(io_wd_IBUF[118]));
  IBUF \io_wd_IBUF[119]_inst 
       (.I(io_wd[119]),
        .O(io_wd_IBUF[119]));
  IBUF \io_wd_IBUF[11]_inst 
       (.I(io_wd[11]),
        .O(io_wd_IBUF[11]));
  IBUF \io_wd_IBUF[120]_inst 
       (.I(io_wd[120]),
        .O(io_wd_IBUF[120]));
  IBUF \io_wd_IBUF[121]_inst 
       (.I(io_wd[121]),
        .O(io_wd_IBUF[121]));
  IBUF \io_wd_IBUF[122]_inst 
       (.I(io_wd[122]),
        .O(io_wd_IBUF[122]));
  IBUF \io_wd_IBUF[123]_inst 
       (.I(io_wd[123]),
        .O(io_wd_IBUF[123]));
  IBUF \io_wd_IBUF[124]_inst 
       (.I(io_wd[124]),
        .O(io_wd_IBUF[124]));
  IBUF \io_wd_IBUF[125]_inst 
       (.I(io_wd[125]),
        .O(io_wd_IBUF[125]));
  IBUF \io_wd_IBUF[126]_inst 
       (.I(io_wd[126]),
        .O(io_wd_IBUF[126]));
  IBUF \io_wd_IBUF[127]_inst 
       (.I(io_wd[127]),
        .O(io_wd_IBUF[127]));
  IBUF \io_wd_IBUF[12]_inst 
       (.I(io_wd[12]),
        .O(io_wd_IBUF[12]));
  IBUF \io_wd_IBUF[13]_inst 
       (.I(io_wd[13]),
        .O(io_wd_IBUF[13]));
  IBUF \io_wd_IBUF[14]_inst 
       (.I(io_wd[14]),
        .O(io_wd_IBUF[14]));
  IBUF \io_wd_IBUF[15]_inst 
       (.I(io_wd[15]),
        .O(io_wd_IBUF[15]));
  IBUF \io_wd_IBUF[16]_inst 
       (.I(io_wd[16]),
        .O(io_wd_IBUF[16]));
  IBUF \io_wd_IBUF[17]_inst 
       (.I(io_wd[17]),
        .O(io_wd_IBUF[17]));
  IBUF \io_wd_IBUF[18]_inst 
       (.I(io_wd[18]),
        .O(io_wd_IBUF[18]));
  IBUF \io_wd_IBUF[19]_inst 
       (.I(io_wd[19]),
        .O(io_wd_IBUF[19]));
  IBUF \io_wd_IBUF[1]_inst 
       (.I(io_wd[1]),
        .O(io_wd_IBUF[1]));
  IBUF \io_wd_IBUF[20]_inst 
       (.I(io_wd[20]),
        .O(io_wd_IBUF[20]));
  IBUF \io_wd_IBUF[21]_inst 
       (.I(io_wd[21]),
        .O(io_wd_IBUF[21]));
  IBUF \io_wd_IBUF[22]_inst 
       (.I(io_wd[22]),
        .O(io_wd_IBUF[22]));
  IBUF \io_wd_IBUF[23]_inst 
       (.I(io_wd[23]),
        .O(io_wd_IBUF[23]));
  IBUF \io_wd_IBUF[24]_inst 
       (.I(io_wd[24]),
        .O(io_wd_IBUF[24]));
  IBUF \io_wd_IBUF[25]_inst 
       (.I(io_wd[25]),
        .O(io_wd_IBUF[25]));
  IBUF \io_wd_IBUF[26]_inst 
       (.I(io_wd[26]),
        .O(io_wd_IBUF[26]));
  IBUF \io_wd_IBUF[27]_inst 
       (.I(io_wd[27]),
        .O(io_wd_IBUF[27]));
  IBUF \io_wd_IBUF[28]_inst 
       (.I(io_wd[28]),
        .O(io_wd_IBUF[28]));
  IBUF \io_wd_IBUF[29]_inst 
       (.I(io_wd[29]),
        .O(io_wd_IBUF[29]));
  IBUF \io_wd_IBUF[2]_inst 
       (.I(io_wd[2]),
        .O(io_wd_IBUF[2]));
  IBUF \io_wd_IBUF[30]_inst 
       (.I(io_wd[30]),
        .O(io_wd_IBUF[30]));
  IBUF \io_wd_IBUF[31]_inst 
       (.I(io_wd[31]),
        .O(io_wd_IBUF[31]));
  IBUF \io_wd_IBUF[32]_inst 
       (.I(io_wd[32]),
        .O(io_wd_IBUF[32]));
  IBUF \io_wd_IBUF[33]_inst 
       (.I(io_wd[33]),
        .O(io_wd_IBUF[33]));
  IBUF \io_wd_IBUF[34]_inst 
       (.I(io_wd[34]),
        .O(io_wd_IBUF[34]));
  IBUF \io_wd_IBUF[35]_inst 
       (.I(io_wd[35]),
        .O(io_wd_IBUF[35]));
  IBUF \io_wd_IBUF[36]_inst 
       (.I(io_wd[36]),
        .O(io_wd_IBUF[36]));
  IBUF \io_wd_IBUF[37]_inst 
       (.I(io_wd[37]),
        .O(io_wd_IBUF[37]));
  IBUF \io_wd_IBUF[38]_inst 
       (.I(io_wd[38]),
        .O(io_wd_IBUF[38]));
  IBUF \io_wd_IBUF[39]_inst 
       (.I(io_wd[39]),
        .O(io_wd_IBUF[39]));
  IBUF \io_wd_IBUF[3]_inst 
       (.I(io_wd[3]),
        .O(io_wd_IBUF[3]));
  IBUF \io_wd_IBUF[40]_inst 
       (.I(io_wd[40]),
        .O(io_wd_IBUF[40]));
  IBUF \io_wd_IBUF[41]_inst 
       (.I(io_wd[41]),
        .O(io_wd_IBUF[41]));
  IBUF \io_wd_IBUF[42]_inst 
       (.I(io_wd[42]),
        .O(io_wd_IBUF[42]));
  IBUF \io_wd_IBUF[43]_inst 
       (.I(io_wd[43]),
        .O(io_wd_IBUF[43]));
  IBUF \io_wd_IBUF[44]_inst 
       (.I(io_wd[44]),
        .O(io_wd_IBUF[44]));
  IBUF \io_wd_IBUF[45]_inst 
       (.I(io_wd[45]),
        .O(io_wd_IBUF[45]));
  IBUF \io_wd_IBUF[46]_inst 
       (.I(io_wd[46]),
        .O(io_wd_IBUF[46]));
  IBUF \io_wd_IBUF[47]_inst 
       (.I(io_wd[47]),
        .O(io_wd_IBUF[47]));
  IBUF \io_wd_IBUF[48]_inst 
       (.I(io_wd[48]),
        .O(io_wd_IBUF[48]));
  IBUF \io_wd_IBUF[49]_inst 
       (.I(io_wd[49]),
        .O(io_wd_IBUF[49]));
  IBUF \io_wd_IBUF[4]_inst 
       (.I(io_wd[4]),
        .O(io_wd_IBUF[4]));
  IBUF \io_wd_IBUF[50]_inst 
       (.I(io_wd[50]),
        .O(io_wd_IBUF[50]));
  IBUF \io_wd_IBUF[51]_inst 
       (.I(io_wd[51]),
        .O(io_wd_IBUF[51]));
  IBUF \io_wd_IBUF[52]_inst 
       (.I(io_wd[52]),
        .O(io_wd_IBUF[52]));
  IBUF \io_wd_IBUF[53]_inst 
       (.I(io_wd[53]),
        .O(io_wd_IBUF[53]));
  IBUF \io_wd_IBUF[54]_inst 
       (.I(io_wd[54]),
        .O(io_wd_IBUF[54]));
  IBUF \io_wd_IBUF[55]_inst 
       (.I(io_wd[55]),
        .O(io_wd_IBUF[55]));
  IBUF \io_wd_IBUF[56]_inst 
       (.I(io_wd[56]),
        .O(io_wd_IBUF[56]));
  IBUF \io_wd_IBUF[57]_inst 
       (.I(io_wd[57]),
        .O(io_wd_IBUF[57]));
  IBUF \io_wd_IBUF[58]_inst 
       (.I(io_wd[58]),
        .O(io_wd_IBUF[58]));
  IBUF \io_wd_IBUF[59]_inst 
       (.I(io_wd[59]),
        .O(io_wd_IBUF[59]));
  IBUF \io_wd_IBUF[5]_inst 
       (.I(io_wd[5]),
        .O(io_wd_IBUF[5]));
  IBUF \io_wd_IBUF[60]_inst 
       (.I(io_wd[60]),
        .O(io_wd_IBUF[60]));
  IBUF \io_wd_IBUF[61]_inst 
       (.I(io_wd[61]),
        .O(io_wd_IBUF[61]));
  IBUF \io_wd_IBUF[62]_inst 
       (.I(io_wd[62]),
        .O(io_wd_IBUF[62]));
  IBUF \io_wd_IBUF[63]_inst 
       (.I(io_wd[63]),
        .O(io_wd_IBUF[63]));
  IBUF \io_wd_IBUF[64]_inst 
       (.I(io_wd[64]),
        .O(io_wd_IBUF[64]));
  IBUF \io_wd_IBUF[65]_inst 
       (.I(io_wd[65]),
        .O(io_wd_IBUF[65]));
  IBUF \io_wd_IBUF[66]_inst 
       (.I(io_wd[66]),
        .O(io_wd_IBUF[66]));
  IBUF \io_wd_IBUF[67]_inst 
       (.I(io_wd[67]),
        .O(io_wd_IBUF[67]));
  IBUF \io_wd_IBUF[68]_inst 
       (.I(io_wd[68]),
        .O(io_wd_IBUF[68]));
  IBUF \io_wd_IBUF[69]_inst 
       (.I(io_wd[69]),
        .O(io_wd_IBUF[69]));
  IBUF \io_wd_IBUF[6]_inst 
       (.I(io_wd[6]),
        .O(io_wd_IBUF[6]));
  IBUF \io_wd_IBUF[70]_inst 
       (.I(io_wd[70]),
        .O(io_wd_IBUF[70]));
  IBUF \io_wd_IBUF[71]_inst 
       (.I(io_wd[71]),
        .O(io_wd_IBUF[71]));
  IBUF \io_wd_IBUF[72]_inst 
       (.I(io_wd[72]),
        .O(io_wd_IBUF[72]));
  IBUF \io_wd_IBUF[73]_inst 
       (.I(io_wd[73]),
        .O(io_wd_IBUF[73]));
  IBUF \io_wd_IBUF[74]_inst 
       (.I(io_wd[74]),
        .O(io_wd_IBUF[74]));
  IBUF \io_wd_IBUF[75]_inst 
       (.I(io_wd[75]),
        .O(io_wd_IBUF[75]));
  IBUF \io_wd_IBUF[76]_inst 
       (.I(io_wd[76]),
        .O(io_wd_IBUF[76]));
  IBUF \io_wd_IBUF[77]_inst 
       (.I(io_wd[77]),
        .O(io_wd_IBUF[77]));
  IBUF \io_wd_IBUF[78]_inst 
       (.I(io_wd[78]),
        .O(io_wd_IBUF[78]));
  IBUF \io_wd_IBUF[79]_inst 
       (.I(io_wd[79]),
        .O(io_wd_IBUF[79]));
  IBUF \io_wd_IBUF[7]_inst 
       (.I(io_wd[7]),
        .O(io_wd_IBUF[7]));
  IBUF \io_wd_IBUF[80]_inst 
       (.I(io_wd[80]),
        .O(io_wd_IBUF[80]));
  IBUF \io_wd_IBUF[81]_inst 
       (.I(io_wd[81]),
        .O(io_wd_IBUF[81]));
  IBUF \io_wd_IBUF[82]_inst 
       (.I(io_wd[82]),
        .O(io_wd_IBUF[82]));
  IBUF \io_wd_IBUF[83]_inst 
       (.I(io_wd[83]),
        .O(io_wd_IBUF[83]));
  IBUF \io_wd_IBUF[84]_inst 
       (.I(io_wd[84]),
        .O(io_wd_IBUF[84]));
  IBUF \io_wd_IBUF[85]_inst 
       (.I(io_wd[85]),
        .O(io_wd_IBUF[85]));
  IBUF \io_wd_IBUF[86]_inst 
       (.I(io_wd[86]),
        .O(io_wd_IBUF[86]));
  IBUF \io_wd_IBUF[87]_inst 
       (.I(io_wd[87]),
        .O(io_wd_IBUF[87]));
  IBUF \io_wd_IBUF[88]_inst 
       (.I(io_wd[88]),
        .O(io_wd_IBUF[88]));
  IBUF \io_wd_IBUF[89]_inst 
       (.I(io_wd[89]),
        .O(io_wd_IBUF[89]));
  IBUF \io_wd_IBUF[8]_inst 
       (.I(io_wd[8]),
        .O(io_wd_IBUF[8]));
  IBUF \io_wd_IBUF[90]_inst 
       (.I(io_wd[90]),
        .O(io_wd_IBUF[90]));
  IBUF \io_wd_IBUF[91]_inst 
       (.I(io_wd[91]),
        .O(io_wd_IBUF[91]));
  IBUF \io_wd_IBUF[92]_inst 
       (.I(io_wd[92]),
        .O(io_wd_IBUF[92]));
  IBUF \io_wd_IBUF[93]_inst 
       (.I(io_wd[93]),
        .O(io_wd_IBUF[93]));
  IBUF \io_wd_IBUF[94]_inst 
       (.I(io_wd[94]),
        .O(io_wd_IBUF[94]));
  IBUF \io_wd_IBUF[95]_inst 
       (.I(io_wd[95]),
        .O(io_wd_IBUF[95]));
  IBUF \io_wd_IBUF[96]_inst 
       (.I(io_wd[96]),
        .O(io_wd_IBUF[96]));
  IBUF \io_wd_IBUF[97]_inst 
       (.I(io_wd[97]),
        .O(io_wd_IBUF[97]));
  IBUF \io_wd_IBUF[98]_inst 
       (.I(io_wd[98]),
        .O(io_wd_IBUF[98]));
  IBUF \io_wd_IBUF[99]_inst 
       (.I(io_wd[99]),
        .O(io_wd_IBUF[99]));
  IBUF \io_wd_IBUF[9]_inst 
       (.I(io_wd[9]),
        .O(io_wd_IBUF[9]));
  IBUF io_we_IBUF_inst
       (.I(io_we),
        .O(io_we_IBUF));
  memory_1024x128 memory_ext
       (.R0_addr(io_ra_IBUF),
        .R0_clk(clock_IBUF_BUFG),
        .R0_data0(io_rd_OBUF),
        .W0_addr(io_wa_IBUF),
        .W0_data(io_wd_IBUF),
        .W0_en(io_we_IBUF));
endmodule

module memory_1024x128
   (R0_data0,
    R0_clk,
    W0_addr,
    R0_addr,
    W0_data,
    W0_en);
  output [127:0]R0_data0;
  input R0_clk;
  input [9:0]W0_addr;
  input [9:0]R0_addr;
  input [127:0]W0_data;
  input W0_en;

  wire [9:0]R0_addr;
  wire R0_clk;
  wire [127:0]R0_data0;
  wire [9:0]W0_addr;
  wire [127:0]W0_data;
  wire W0_en;
  wire NLW_Memory_reg_uram_0_CAS_OUT_DBITERR_A_UNCONNECTED;
  wire NLW_Memory_reg_uram_0_CAS_OUT_DBITERR_B_UNCONNECTED;
  wire NLW_Memory_reg_uram_0_CAS_OUT_EN_A_UNCONNECTED;
  wire NLW_Memory_reg_uram_0_CAS_OUT_EN_B_UNCONNECTED;
  wire NLW_Memory_reg_uram_0_CAS_OUT_RDACCESS_A_UNCONNECTED;
  wire NLW_Memory_reg_uram_0_CAS_OUT_RDACCESS_B_UNCONNECTED;
  wire NLW_Memory_reg_uram_0_CAS_OUT_RDB_WR_A_UNCONNECTED;
  wire NLW_Memory_reg_uram_0_CAS_OUT_RDB_WR_B_UNCONNECTED;
  wire NLW_Memory_reg_uram_0_CAS_OUT_SBITERR_A_UNCONNECTED;
  wire NLW_Memory_reg_uram_0_CAS_OUT_SBITERR_B_UNCONNECTED;
  wire NLW_Memory_reg_uram_0_DBITERR_A_UNCONNECTED;
  wire NLW_Memory_reg_uram_0_DBITERR_B_UNCONNECTED;
  wire NLW_Memory_reg_uram_0_RDACCESS_A_UNCONNECTED;
  wire NLW_Memory_reg_uram_0_RDACCESS_B_UNCONNECTED;
  wire NLW_Memory_reg_uram_0_SBITERR_A_UNCONNECTED;
  wire NLW_Memory_reg_uram_0_SBITERR_B_UNCONNECTED;
  wire [22:0]NLW_Memory_reg_uram_0_CAS_OUT_ADDR_A_UNCONNECTED;
  wire [22:0]NLW_Memory_reg_uram_0_CAS_OUT_ADDR_B_UNCONNECTED;
  wire [8:0]NLW_Memory_reg_uram_0_CAS_OUT_BWE_A_UNCONNECTED;
  wire [8:0]NLW_Memory_reg_uram_0_CAS_OUT_BWE_B_UNCONNECTED;
  wire [71:0]NLW_Memory_reg_uram_0_CAS_OUT_DIN_A_UNCONNECTED;
  wire [71:0]NLW_Memory_reg_uram_0_CAS_OUT_DIN_B_UNCONNECTED;
  wire [71:0]NLW_Memory_reg_uram_0_CAS_OUT_DOUT_A_UNCONNECTED;
  wire [71:0]NLW_Memory_reg_uram_0_CAS_OUT_DOUT_B_UNCONNECTED;
  wire [71:0]NLW_Memory_reg_uram_0_DOUT_A_UNCONNECTED;
  wire NLW_Memory_reg_uram_1_CAS_OUT_DBITERR_A_UNCONNECTED;
  wire NLW_Memory_reg_uram_1_CAS_OUT_DBITERR_B_UNCONNECTED;
  wire NLW_Memory_reg_uram_1_CAS_OUT_EN_A_UNCONNECTED;
  wire NLW_Memory_reg_uram_1_CAS_OUT_EN_B_UNCONNECTED;
  wire NLW_Memory_reg_uram_1_CAS_OUT_RDACCESS_A_UNCONNECTED;
  wire NLW_Memory_reg_uram_1_CAS_OUT_RDACCESS_B_UNCONNECTED;
  wire NLW_Memory_reg_uram_1_CAS_OUT_RDB_WR_A_UNCONNECTED;
  wire NLW_Memory_reg_uram_1_CAS_OUT_RDB_WR_B_UNCONNECTED;
  wire NLW_Memory_reg_uram_1_CAS_OUT_SBITERR_A_UNCONNECTED;
  wire NLW_Memory_reg_uram_1_CAS_OUT_SBITERR_B_UNCONNECTED;
  wire NLW_Memory_reg_uram_1_DBITERR_A_UNCONNECTED;
  wire NLW_Memory_reg_uram_1_DBITERR_B_UNCONNECTED;
  wire NLW_Memory_reg_uram_1_RDACCESS_A_UNCONNECTED;
  wire NLW_Memory_reg_uram_1_RDACCESS_B_UNCONNECTED;
  wire NLW_Memory_reg_uram_1_SBITERR_A_UNCONNECTED;
  wire NLW_Memory_reg_uram_1_SBITERR_B_UNCONNECTED;
  wire [22:0]NLW_Memory_reg_uram_1_CAS_OUT_ADDR_A_UNCONNECTED;
  wire [22:0]NLW_Memory_reg_uram_1_CAS_OUT_ADDR_B_UNCONNECTED;
  wire [8:0]NLW_Memory_reg_uram_1_CAS_OUT_BWE_A_UNCONNECTED;
  wire [8:0]NLW_Memory_reg_uram_1_CAS_OUT_BWE_B_UNCONNECTED;
  wire [71:0]NLW_Memory_reg_uram_1_CAS_OUT_DIN_A_UNCONNECTED;
  wire [71:0]NLW_Memory_reg_uram_1_CAS_OUT_DIN_B_UNCONNECTED;
  wire [71:0]NLW_Memory_reg_uram_1_CAS_OUT_DOUT_A_UNCONNECTED;
  wire [71:0]NLW_Memory_reg_uram_1_CAS_OUT_DOUT_B_UNCONNECTED;
  wire [71:0]NLW_Memory_reg_uram_1_DOUT_A_UNCONNECTED;
  wire [71:56]NLW_Memory_reg_uram_1_DOUT_B_UNCONNECTED;

  (* \MEM.PORTA.ADDRESS_BEGIN  = "0" *) 
  (* \MEM.PORTA.ADDRESS_END  = "1023" *) 
  (* \MEM.PORTA.DATA_LSB  = "0" *) 
  (* \MEM.PORTA.DATA_MSB  = "71" *) 
  (* \MEM.PORTB.ADDRESS_BEGIN  = "0" *) 
  (* \MEM.PORTB.ADDRESS_END  = "1023" *) 
  (* \MEM.PORTB.DATA_LSB  = "0" *) 
  (* \MEM.PORTB.DATA_MSB  = "71" *) 
  (* METHODOLOGY_DRC_VIOS = "{SYNTH-6 {cell *THIS*}}" *) 
  (* RTL_RAM_BITS = "131072" *) 
  (* RTL_RAM_NAME = "UltraStoreProbe/memory_ext/Memory_reg" *) 
  (* RTL_RAM_STYLE = "ultra" *) 
  (* RTL_RAM_TYPE = "RAM_SDP" *) 
  (* ram_addr_begin = "0" *) 
  (* ram_addr_end = "1023" *) 
  (* ram_slice_begin = "0" *) 
  (* ram_slice_end = "71" *) 
  URAM288 #(
    .AVG_CONS_INACTIVE_CYCLES(10),
    .BWE_MODE_A("PARITY_INDEPENDENT"),
    .BWE_MODE_B("PARITY_INDEPENDENT"),
    .CASCADE_ORDER_A("NONE"),
    .CASCADE_ORDER_B("NONE"),
    .EN_ECC_RD_A("FALSE"),
    .EN_ECC_RD_B("FALSE"),
    .EN_ECC_WR_A("FALSE"),
    .EN_ECC_WR_B("FALSE"),
    .IREG_PRE_A("FALSE"),
    .IREG_PRE_B("FALSE"),
    .IS_CLK_INVERTED(1'b0),
    .IS_EN_A_INVERTED(1'b0),
    .IS_EN_B_INVERTED(1'b0),
    .IS_RDB_WR_A_INVERTED(1'b0),
    .IS_RDB_WR_B_INVERTED(1'b0),
    .IS_RST_A_INVERTED(1'b0),
    .IS_RST_B_INVERTED(1'b0),
    .MATRIX_ID("memory_ext/Memory_reg_B3_M0"),
    .NUM_UNIQUE_SELF_ADDR_A(1),
    .NUM_UNIQUE_SELF_ADDR_B(1),
    .NUM_URAM_IN_MATRIX(1),
    .OREG_A("FALSE"),
    .OREG_B("FALSE"),
    .OREG_ECC_A("FALSE"),
    .OREG_ECC_B("FALSE"),
    .REG_CAS_A("FALSE"),
    .REG_CAS_B("FALSE"),
    .RST_MODE_A("SYNC"),
    .RST_MODE_B("SYNC"),
    .SELF_ADDR_A(11'h000),
    .SELF_ADDR_B(11'h000),
    .SELF_MASK_A(11'h7FF),
    .SELF_MASK_B(11'h7FF),
    .USE_EXT_CE_A("FALSE"),
    .USE_EXT_CE_B("FALSE")) 
    Memory_reg_uram_0
       (.ADDR_A({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,W0_addr}),
        .ADDR_B({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,R0_addr}),
        .BWE_A({W0_en,W0_en,W0_en,W0_en,W0_en,W0_en,W0_en,W0_en,W0_en}),
        .BWE_B({1'b1,1'b1,1'b1,1'b1,1'b1,1'b1,1'b1,1'b1,1'b1}),
        .CAS_IN_ADDR_A({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0}),
        .CAS_IN_ADDR_B({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0}),
        .CAS_IN_BWE_A({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0}),
        .CAS_IN_BWE_B({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0}),
        .CAS_IN_DBITERR_A(1'b0),
        .CAS_IN_DBITERR_B(1'b0),
        .CAS_IN_DIN_A({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0}),
        .CAS_IN_DIN_B({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0}),
        .CAS_IN_DOUT_A({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0}),
        .CAS_IN_DOUT_B({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0}),
        .CAS_IN_EN_A(1'b0),
        .CAS_IN_EN_B(1'b0),
        .CAS_IN_RDACCESS_A(1'b0),
        .CAS_IN_RDACCESS_B(1'b0),
        .CAS_IN_RDB_WR_A(1'b0),
        .CAS_IN_RDB_WR_B(1'b0),
        .CAS_IN_SBITERR_A(1'b0),
        .CAS_IN_SBITERR_B(1'b0),
        .CAS_OUT_ADDR_A(NLW_Memory_reg_uram_0_CAS_OUT_ADDR_A_UNCONNECTED[22:0]),
        .CAS_OUT_ADDR_B(NLW_Memory_reg_uram_0_CAS_OUT_ADDR_B_UNCONNECTED[22:0]),
        .CAS_OUT_BWE_A(NLW_Memory_reg_uram_0_CAS_OUT_BWE_A_UNCONNECTED[8:0]),
        .CAS_OUT_BWE_B(NLW_Memory_reg_uram_0_CAS_OUT_BWE_B_UNCONNECTED[8:0]),
        .CAS_OUT_DBITERR_A(NLW_Memory_reg_uram_0_CAS_OUT_DBITERR_A_UNCONNECTED),
        .CAS_OUT_DBITERR_B(NLW_Memory_reg_uram_0_CAS_OUT_DBITERR_B_UNCONNECTED),
        .CAS_OUT_DIN_A(NLW_Memory_reg_uram_0_CAS_OUT_DIN_A_UNCONNECTED[71:0]),
        .CAS_OUT_DIN_B(NLW_Memory_reg_uram_0_CAS_OUT_DIN_B_UNCONNECTED[71:0]),
        .CAS_OUT_DOUT_A(NLW_Memory_reg_uram_0_CAS_OUT_DOUT_A_UNCONNECTED[71:0]),
        .CAS_OUT_DOUT_B(NLW_Memory_reg_uram_0_CAS_OUT_DOUT_B_UNCONNECTED[71:0]),
        .CAS_OUT_EN_A(NLW_Memory_reg_uram_0_CAS_OUT_EN_A_UNCONNECTED),
        .CAS_OUT_EN_B(NLW_Memory_reg_uram_0_CAS_OUT_EN_B_UNCONNECTED),
        .CAS_OUT_RDACCESS_A(NLW_Memory_reg_uram_0_CAS_OUT_RDACCESS_A_UNCONNECTED),
        .CAS_OUT_RDACCESS_B(NLW_Memory_reg_uram_0_CAS_OUT_RDACCESS_B_UNCONNECTED),
        .CAS_OUT_RDB_WR_A(NLW_Memory_reg_uram_0_CAS_OUT_RDB_WR_A_UNCONNECTED),
        .CAS_OUT_RDB_WR_B(NLW_Memory_reg_uram_0_CAS_OUT_RDB_WR_B_UNCONNECTED),
        .CAS_OUT_SBITERR_A(NLW_Memory_reg_uram_0_CAS_OUT_SBITERR_A_UNCONNECTED),
        .CAS_OUT_SBITERR_B(NLW_Memory_reg_uram_0_CAS_OUT_SBITERR_B_UNCONNECTED),
        .CLK(R0_clk),
        .DBITERR_A(NLW_Memory_reg_uram_0_DBITERR_A_UNCONNECTED),
        .DBITERR_B(NLW_Memory_reg_uram_0_DBITERR_B_UNCONNECTED),
        .DIN_A(W0_data[71:0]),
        .DIN_B({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0}),
        .DOUT_A(NLW_Memory_reg_uram_0_DOUT_A_UNCONNECTED[71:0]),
        .DOUT_B(R0_data0[71:0]),
        .EN_A(1'b1),
        .EN_B(1'b1),
        .INJECT_DBITERR_A(1'b0),
        .INJECT_DBITERR_B(1'b0),
        .INJECT_SBITERR_A(1'b0),
        .INJECT_SBITERR_B(1'b0),
        .OREG_CE_A(1'b1),
        .OREG_CE_B(1'b1),
        .OREG_ECC_CE_A(1'b1),
        .OREG_ECC_CE_B(1'b1),
        .RDACCESS_A(NLW_Memory_reg_uram_0_RDACCESS_A_UNCONNECTED),
        .RDACCESS_B(NLW_Memory_reg_uram_0_RDACCESS_B_UNCONNECTED),
        .RDB_WR_A(1'b1),
        .RDB_WR_B(1'b0),
        .RST_A(1'b0),
        .RST_B(1'b0),
        .SBITERR_A(NLW_Memory_reg_uram_0_SBITERR_A_UNCONNECTED),
        .SBITERR_B(NLW_Memory_reg_uram_0_SBITERR_B_UNCONNECTED),
        .SLEEP(1'b0));
  (* \MEM.PORTA.ADDRESS_BEGIN  = "0" *) 
  (* \MEM.PORTA.ADDRESS_END  = "1023" *) 
  (* \MEM.PORTA.DATA_LSB  = "72" *) 
  (* \MEM.PORTA.DATA_MSB  = "127" *) 
  (* \MEM.PORTB.ADDRESS_BEGIN  = "0" *) 
  (* \MEM.PORTB.ADDRESS_END  = "1023" *) 
  (* \MEM.PORTB.DATA_LSB  = "72" *) 
  (* \MEM.PORTB.DATA_MSB  = "127" *) 
  (* METHODOLOGY_DRC_VIOS = "{SYNTH-6 {cell *THIS*}}" *) 
  (* RTL_RAM_BITS = "131072" *) 
  (* RTL_RAM_NAME = "UltraStoreProbe/memory_ext/Memory_reg" *) 
  (* RTL_RAM_STYLE = "ultra" *) 
  (* RTL_RAM_TYPE = "RAM_SDP" *) 
  (* ram_addr_begin = "0" *) 
  (* ram_addr_end = "1023" *) 
  (* ram_slice_begin = "72" *) 
  (* ram_slice_end = "127" *) 
  URAM288 #(
    .AVG_CONS_INACTIVE_CYCLES(10),
    .BWE_MODE_A("PARITY_INDEPENDENT"),
    .BWE_MODE_B("PARITY_INDEPENDENT"),
    .CASCADE_ORDER_A("NONE"),
    .CASCADE_ORDER_B("NONE"),
    .EN_ECC_RD_A("FALSE"),
    .EN_ECC_RD_B("FALSE"),
    .EN_ECC_WR_A("FALSE"),
    .EN_ECC_WR_B("FALSE"),
    .IREG_PRE_A("FALSE"),
    .IREG_PRE_B("FALSE"),
    .IS_CLK_INVERTED(1'b0),
    .IS_EN_A_INVERTED(1'b0),
    .IS_EN_B_INVERTED(1'b0),
    .IS_RDB_WR_A_INVERTED(1'b0),
    .IS_RDB_WR_B_INVERTED(1'b0),
    .IS_RST_A_INVERTED(1'b0),
    .IS_RST_B_INVERTED(1'b0),
    .MATRIX_ID("memory_ext/Memory_reg_B3_M1"),
    .NUM_UNIQUE_SELF_ADDR_A(1),
    .NUM_UNIQUE_SELF_ADDR_B(1),
    .NUM_URAM_IN_MATRIX(1),
    .OREG_A("FALSE"),
    .OREG_B("FALSE"),
    .OREG_ECC_A("FALSE"),
    .OREG_ECC_B("FALSE"),
    .REG_CAS_A("FALSE"),
    .REG_CAS_B("FALSE"),
    .RST_MODE_A("SYNC"),
    .RST_MODE_B("SYNC"),
    .SELF_ADDR_A(11'h000),
    .SELF_ADDR_B(11'h000),
    .SELF_MASK_A(11'h7FF),
    .SELF_MASK_B(11'h7FF),
    .USE_EXT_CE_A("FALSE"),
    .USE_EXT_CE_B("FALSE")) 
    Memory_reg_uram_1
       (.ADDR_A({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,W0_addr}),
        .ADDR_B({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,R0_addr}),
        .BWE_A({W0_en,W0_en,W0_en,W0_en,W0_en,W0_en,W0_en,W0_en,W0_en}),
        .BWE_B({1'b1,1'b1,1'b1,1'b1,1'b1,1'b1,1'b1,1'b1,1'b1}),
        .CAS_IN_ADDR_A({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0}),
        .CAS_IN_ADDR_B({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0}),
        .CAS_IN_BWE_A({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0}),
        .CAS_IN_BWE_B({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0}),
        .CAS_IN_DBITERR_A(1'b0),
        .CAS_IN_DBITERR_B(1'b0),
        .CAS_IN_DIN_A({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0}),
        .CAS_IN_DIN_B({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0}),
        .CAS_IN_DOUT_A({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0}),
        .CAS_IN_DOUT_B({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0}),
        .CAS_IN_EN_A(1'b0),
        .CAS_IN_EN_B(1'b0),
        .CAS_IN_RDACCESS_A(1'b0),
        .CAS_IN_RDACCESS_B(1'b0),
        .CAS_IN_RDB_WR_A(1'b0),
        .CAS_IN_RDB_WR_B(1'b0),
        .CAS_IN_SBITERR_A(1'b0),
        .CAS_IN_SBITERR_B(1'b0),
        .CAS_OUT_ADDR_A(NLW_Memory_reg_uram_1_CAS_OUT_ADDR_A_UNCONNECTED[22:0]),
        .CAS_OUT_ADDR_B(NLW_Memory_reg_uram_1_CAS_OUT_ADDR_B_UNCONNECTED[22:0]),
        .CAS_OUT_BWE_A(NLW_Memory_reg_uram_1_CAS_OUT_BWE_A_UNCONNECTED[8:0]),
        .CAS_OUT_BWE_B(NLW_Memory_reg_uram_1_CAS_OUT_BWE_B_UNCONNECTED[8:0]),
        .CAS_OUT_DBITERR_A(NLW_Memory_reg_uram_1_CAS_OUT_DBITERR_A_UNCONNECTED),
        .CAS_OUT_DBITERR_B(NLW_Memory_reg_uram_1_CAS_OUT_DBITERR_B_UNCONNECTED),
        .CAS_OUT_DIN_A(NLW_Memory_reg_uram_1_CAS_OUT_DIN_A_UNCONNECTED[71:0]),
        .CAS_OUT_DIN_B(NLW_Memory_reg_uram_1_CAS_OUT_DIN_B_UNCONNECTED[71:0]),
        .CAS_OUT_DOUT_A(NLW_Memory_reg_uram_1_CAS_OUT_DOUT_A_UNCONNECTED[71:0]),
        .CAS_OUT_DOUT_B(NLW_Memory_reg_uram_1_CAS_OUT_DOUT_B_UNCONNECTED[71:0]),
        .CAS_OUT_EN_A(NLW_Memory_reg_uram_1_CAS_OUT_EN_A_UNCONNECTED),
        .CAS_OUT_EN_B(NLW_Memory_reg_uram_1_CAS_OUT_EN_B_UNCONNECTED),
        .CAS_OUT_RDACCESS_A(NLW_Memory_reg_uram_1_CAS_OUT_RDACCESS_A_UNCONNECTED),
        .CAS_OUT_RDACCESS_B(NLW_Memory_reg_uram_1_CAS_OUT_RDACCESS_B_UNCONNECTED),
        .CAS_OUT_RDB_WR_A(NLW_Memory_reg_uram_1_CAS_OUT_RDB_WR_A_UNCONNECTED),
        .CAS_OUT_RDB_WR_B(NLW_Memory_reg_uram_1_CAS_OUT_RDB_WR_B_UNCONNECTED),
        .CAS_OUT_SBITERR_A(NLW_Memory_reg_uram_1_CAS_OUT_SBITERR_A_UNCONNECTED),
        .CAS_OUT_SBITERR_B(NLW_Memory_reg_uram_1_CAS_OUT_SBITERR_B_UNCONNECTED),
        .CLK(R0_clk),
        .DBITERR_A(NLW_Memory_reg_uram_1_DBITERR_A_UNCONNECTED),
        .DBITERR_B(NLW_Memory_reg_uram_1_DBITERR_B_UNCONNECTED),
        .DIN_A({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,W0_data[127:72]}),
        .DIN_B({1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0,1'b0}),
        .DOUT_A(NLW_Memory_reg_uram_1_DOUT_A_UNCONNECTED[71:0]),
        .DOUT_B({NLW_Memory_reg_uram_1_DOUT_B_UNCONNECTED[71:56],R0_data0[127:72]}),
        .EN_A(1'b1),
        .EN_B(1'b1),
        .INJECT_DBITERR_A(1'b0),
        .INJECT_DBITERR_B(1'b0),
        .INJECT_SBITERR_A(1'b0),
        .INJECT_SBITERR_B(1'b0),
        .OREG_CE_A(1'b1),
        .OREG_CE_B(1'b1),
        .OREG_ECC_CE_A(1'b1),
        .OREG_ECC_CE_B(1'b1),
        .RDACCESS_A(NLW_Memory_reg_uram_1_RDACCESS_A_UNCONNECTED),
        .RDACCESS_B(NLW_Memory_reg_uram_1_RDACCESS_B_UNCONNECTED),
        .RDB_WR_A(1'b1),
        .RDB_WR_B(1'b0),
        .RST_A(1'b0),
        .RST_B(1'b0),
        .SBITERR_A(NLW_Memory_reg_uram_1_SBITERR_A_UNCONNECTED),
        .SBITERR_B(NLW_Memory_reg_uram_1_SBITERR_B_UNCONNECTED),
        .SLEEP(1'b0));
endmodule
`ifndef GLBL
`define GLBL
`timescale  1 ps / 1 ps

module glbl ();

    parameter ROC_WIDTH = 100000;
    parameter TOC_WIDTH = 0;
    parameter GRES_WIDTH = 10000;
    parameter GRES_START = 10000;

//--------   STARTUP Globals --------------
    wire GSR;
    wire GTS;
    wire GWE;
    wire PRLD;
    wire GRESTORE;
    tri1 p_up_tmp;
    tri (weak1, strong0) PLL_LOCKG = p_up_tmp;

    wire PROGB_GLBL;
    wire CCLKO_GLBL;
    wire FCSBO_GLBL;
    wire [3:0] DO_GLBL;
    wire [3:0] DI_GLBL;
   
    reg GSR_int;
    reg GTS_int;
    reg PRLD_int;
    reg GRESTORE_int;

//--------   JTAG Globals --------------
    wire JTAG_TDO_GLBL;
    wire JTAG_TCK_GLBL;
    wire JTAG_TDI_GLBL;
    wire JTAG_TMS_GLBL;
    wire JTAG_TRST_GLBL;

    reg JTAG_CAPTURE_GLBL;
    reg JTAG_RESET_GLBL;
    reg JTAG_SHIFT_GLBL;
    reg JTAG_UPDATE_GLBL;
    reg JTAG_RUNTEST_GLBL;

    reg JTAG_SEL1_GLBL = 0;
    reg JTAG_SEL2_GLBL = 0 ;
    reg JTAG_SEL3_GLBL = 0;
    reg JTAG_SEL4_GLBL = 0;

    reg JTAG_USER_TDO1_GLBL = 1'bz;
    reg JTAG_USER_TDO2_GLBL = 1'bz;
    reg JTAG_USER_TDO3_GLBL = 1'bz;
    reg JTAG_USER_TDO4_GLBL = 1'bz;

    assign (strong1, weak0) GSR = GSR_int;
    assign (strong1, weak0) GTS = GTS_int;
    assign (weak1, weak0) PRLD = PRLD_int;
    assign (strong1, weak0) GRESTORE = GRESTORE_int;

    initial begin
	GSR_int = 1'b1;
	PRLD_int = 1'b1;
	#(ROC_WIDTH)
	GSR_int = 1'b0;
	PRLD_int = 1'b0;
    end

    initial begin
	GTS_int = 1'b1;
	#(TOC_WIDTH)
	GTS_int = 1'b0;
    end

    initial begin 
	GRESTORE_int = 1'b0;
	#(GRES_START);
	GRESTORE_int = 1'b1;
	#(GRES_WIDTH);
	GRESTORE_int = 1'b0;
    end

endmodule
`endif
