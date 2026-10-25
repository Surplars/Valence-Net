package netstack

import chisel3._
import chisel3.util._

/** Flattened verification ports; never part of the production hierarchy. */
class SharedTcpReceiveHarness extends Module {
    val io = IO(new Bundle {
        val valid = Input(Bool()); val ready = Output(Bool())
        val lo = Input(UInt(64.W)); val hi = Input(UInt(64.W)); val keep = Input(UInt(16.W))
        val last = Input(Bool()); val error = Input(Bool()); val tag = Input(UInt(64.W))
        val metaReady = Input(Bool()); val metaValid = Output(Bool())
        val metaTag = Output(UInt(64.W)); val sequence = Output(UInt(32.W))
        val length = Output(UInt(16.W))
        val outReady = Input(Bool()); val outValid = Output(Bool())
        val outLo = Output(UInt(64.W)); val outHi = Output(UInt(64.W))
        val outKeep = Output(UInt(16.W)); val outLast = Output(Bool()); val outError = Output(Bool())
        val dropMask = Output(UInt(3.W))
        val drop0 = Output(UInt(64.W)); val drop1 = Output(UInt(64.W)); val drop2 = Output(UInt(64.W))
    })
    val dut = Module(new SharedTcpReceive(32))
    dut.io.localIp := "h0a000001".U
    dut.io.in.valid := io.valid; io.ready := dut.io.in.ready
    dut.io.in.bits.data := Cat(io.hi, io.lo); dut.io.in.bits.keep := io.keep
    dut.io.in.bits.last := io.last; dut.io.in.bits.error := io.error
    dut.io.inContext := io.tag
    dut.io.metadata.ready := io.metaReady; io.metaValid := dut.io.metadata.valid
    io.metaTag := dut.io.metadataContext
    io.sequence := dut.io.metadata.bits.sequence; io.length := dut.io.metadata.bits.payloadLength
    dut.io.payload.ready := io.outReady; io.outValid := dut.io.payload.valid
    io.outLo := dut.io.payload.bits.data(63, 0); io.outHi := dut.io.payload.bits.data(127, 64)
    io.outKeep := dut.io.payload.bits.keep; io.outLast := dut.io.payload.bits.last
    io.outError := dut.io.payload.bits.error
    io.dropMask := VecInit(dut.io.drops.map(_.valid)).asUInt
    io.drop0 := dut.io.drops(0).bits; io.drop1 := dut.io.drops(1).bits; io.drop2 := dut.io.drops(2).bits
}
