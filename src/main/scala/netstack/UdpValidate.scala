package netstack

import chisel3._
import chisel3.util._

class UdpVerdict extends Bundle {
    val sourcePort = UInt(16.W)
    val destinationPort = UInt(16.W)
    val payloadLength = UInt(16.W)
    val checksumPresent = Bool()
    // Bit 0: malformed stream/MAC error; bit 1: length/header; bit 2: checksum.
    val errors = UInt(3.W)
}

/** IPv4 UDP receive validation sidecar. Input starts at the UDP header.
  * All nonfinal beats must be full; final keep is a nonzero low-bit prefix.
  * IPv4 source, destination and payload length are sampled with the first beat.
  * An upstream packet store must retain the datagram until this verdict fires;
  * the application must never observe uncommitted bytes.
  * The initial fast path requires UDP length == IPv4 payload length. IP fragments
  * must be reassembled or rejected upstream, never presented as full datagrams.
  */
class UdpValidate extends Module {
    val io = IO(new Bundle {
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val sourceIp = Input(UInt(32.W))
        val destinationIp = Input(UInt(32.W))
        val ipPayloadLength = Input(UInt(16.W))
        val out = Decoupled(new UdpVerdict)
    })
    val active = RegInit(false.B)
    val waiting = RegInit(false.B)
    val count = RegInit(0.U(17.W))
    val declared = Reg(UInt(16.W))
    val ipLength = Reg(UInt(16.W))
    val sourcePort = Reg(UInt(16.W))
    val destinationPort = Reg(UInt(16.W))
    val checksumPresent = Reg(Bool())
    val malformed = RegInit(false.B)
    val badLength = RegInit(false.B)

    def word(byte: Int): UInt = Cat(io.in.bits.data(byte * 8 + 7, byte * 8),
        io.in.bits.data(byte * 8 + 15, byte * 8 + 8))
    val firstLength = word(4)
    val pseudo = OnesComplement.sum16(Seq(io.sourceIp(31, 16), io.sourceIp(15, 0),
        io.destinationIp(31, 16), io.destinationIp(15, 0), io.ipPayloadLength, 17.U(16.W)))
    val checksum = Module(new InternetChecksum(16))
    checksum.io.seed := pseudo
    checksum.io.in.bits := io.in.bits
    checksum.io.in.valid := io.in.valid && !waiting
    io.in.ready := checksum.io.in.ready && !waiting

    val length = Mux(active, declared, firstLength)
    val expected = Mux(active, ipLength, io.ipPayloadLength)
    val nextCount = Mux(active, count, 0.U) +& PopCount(io.in.bits.keep)
    val streamBad = (io.in.bits.keep === 0.U) ||
        (!io.in.bits.last && io.in.bits.keep =/= "hffff".U)
    val headerBad = !active && ((io.in.bits.keep & 255.U) =/= 255.U || firstLength < 8.U)
    when(io.in.fire) {
        when(!active) {
            declared := firstLength
            ipLength := io.ipPayloadLength
            sourcePort := word(0)
            destinationPort := word(2)
            checksumPresent := word(6) =/= 0.U
        }
        count := Mux(nextCount > 131071.U, 131071.U, nextCount)
        malformed := (active && malformed) || streamBad
        badLength := (active && badLength) || headerBad || length =/= expected ||
            (io.in.bits.last && nextCount =/= length)
        active := !io.in.bits.last
        when(io.in.bits.last) { waiting := true.B }
    }
    io.out.valid := waiting && checksum.io.out.valid
    io.out.bits.sourcePort := sourcePort
    io.out.bits.destinationPort := destinationPort
    io.out.bits.payloadLength := Mux(declared >= 8.U, declared - 8.U, 0.U)
    io.out.bits.checksumPresent := checksumPresent
    io.out.bits.errors := Cat(checksumPresent && checksum.io.out.bits.checksum =/= 0.U,
        badLength, malformed || checksum.io.out.bits.error)
    checksum.io.out.ready := waiting && io.out.ready
    when(io.out.fire) { waiting := false.B }
}

class UdpHarness extends Module {
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
        val outReady = Input(Bool())
        val outValid = Output(Bool())
        val sourcePort = Output(UInt(16.W))
        val destinationPort = Output(UInt(16.W))
        val payloadLength = Output(UInt(16.W))
        val checksumPresent = Output(Bool())
        val errors = Output(UInt(3.W))
    })
    val dut = Module(new UdpValidate)
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
    io.sourcePort := dut.io.out.bits.sourcePort
    io.destinationPort := dut.io.out.bits.destinationPort
    io.payloadLength := dut.io.out.bits.payloadLength
    io.checksumPresent := dut.io.out.bits.checksumPresent
    io.errors := dut.io.out.bits.errors
}
