package netstack

import chisel3._
import chisel3.util._

/** Validated IPv4 UDP datagram queue; retains the eight-byte UDP header.
  * The source/destination address context follows every output beat. Header
  * removal and application/DMA delivery are separate consumers of this queue.
  * Upstream IPv4 must supply an unfragmented, length-delimited UDP datagram.
  */
class UdpReceive(val beatsPerPacket: Int = 1024) extends Module {
    val io = IO(new Bundle {
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val sourceIp = Input(UInt(32.W))
        val destinationIp = Input(UInt(32.W))
        val ipPayloadLength = Input(UInt(16.W))
        val out = Decoupled(new PacketBeat(16))
        val outSourceIp = Output(UInt(32.W))
        val outDestinationIp = Output(UInt(32.W))
        val dropped = Output(Bool())
    })
    val validator = Module(new UdpValidate)
    val buffer = Module(new PacketCommitBuffer(16, beatsPerPacket))
    validator.io.sourceIp := io.sourceIp
    validator.io.destinationIp := io.destinationIp
    validator.io.ipPayloadLength := io.ipPayloadLength
    buffer.io.inContext := Cat(io.sourceIp, io.destinationIp)
    // Atomic fork: each accepted beat is consumed by both, exactly once.
    io.in.ready := validator.io.in.ready && buffer.io.in.ready
    validator.io.in.bits := io.in.bits
    buffer.io.in.bits := io.in.bits
    validator.io.in.valid := io.in.valid && buffer.io.in.ready
    buffer.io.in.valid := io.in.valid && validator.io.in.ready
    buffer.io.verdict.valid := validator.io.out.valid
    buffer.io.verdict.bits := validator.io.out.bits.errors === 0.U
    validator.io.out.ready := buffer.io.verdict.ready
    io.out <> buffer.io.out
    io.outSourceIp := buffer.io.outContext(63, 32)
    io.outDestinationIp := buffer.io.outContext(31, 0)
    io.dropped := buffer.io.dropped
}

class ReceiveHarness extends Module {
    val io = IO(new Bundle {
        val lo = Input(UInt(64.W))
        val hi = Input(UInt(64.W))
        val keep = Input(UInt(16.W))
        val last = Input(Bool())
        val error = Input(Bool())
        val valid = Input(Bool())
        val ready = Output(Bool())
        val sourceIp = Input(UInt(32.W))
        val destinationIp = Input(UInt(32.W))
        val ipLength = Input(UInt(16.W))
        val dropped = Output(Bool())
        val outReady = Input(Bool())
        val outValid = Output(Bool())
        val outLo = Output(UInt(64.W))
        val outHi = Output(UInt(64.W))
        val outKeep = Output(UInt(16.W))
        val outLast = Output(Bool())
        val outSourceIp = Output(UInt(32.W))
        val outDestinationIp = Output(UInt(32.W))
    })
    val dut = Module(new UdpReceive(16))
    dut.io.in.bits.data := Cat(io.hi, io.lo)
    dut.io.in.bits.keep := io.keep
    dut.io.in.bits.last := io.last
    dut.io.in.bits.error := io.error
    dut.io.in.valid := io.valid
    io.ready := dut.io.in.ready
    dut.io.sourceIp := io.sourceIp
    dut.io.destinationIp := io.destinationIp
    dut.io.ipPayloadLength := io.ipLength
    dut.io.out.ready := io.outReady
    io.outValid := dut.io.out.valid
    io.outLo := dut.io.out.bits.data(63, 0)
    io.outHi := dut.io.out.bits.data(127, 64)
    io.outKeep := dut.io.out.bits.keep
    io.outLast := dut.io.out.bits.last
    io.outSourceIp := dut.io.outSourceIp
    io.outDestinationIp := dut.io.outDestinationIp
    io.dropped := dut.io.dropped
}
