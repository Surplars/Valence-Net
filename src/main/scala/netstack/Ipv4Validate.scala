package netstack

import chisel3._
import chisel3.util._

class Ipv4Verdict extends Bundle {
    val source = UInt(32.W)
    val destination = UInt(32.W)
    val protocol = UInt(8.W)
    val headerLength = UInt(6.W)
    val totalLength = UInt(16.W)
    // stream, header/length, checksum, fragmentation, destination, options
    val errors = UInt(6.W)
}

/** IPv4 validator for the unfragmented fast path, beginning at the IP header.
  * Full nonfinal beats, low-prefix final keep. Link padding after totalLength
  * is legal; it is removed by PacketSlice after the entire packet is validated.
  * Options are checksummed but rejected until their semantics are implemented.
  * Config localIp/acceptBroadcast is sampled at packet start.
  */
class Ipv4Validate extends Module {
    val io = IO(new Bundle {
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val localIp = Input(UInt(32.W))
        val acceptBroadcast = Input(Bool())
        val out = Decoupled(new Ipv4Verdict)
    })
    def byte(i: Int): UInt = io.in.bits.data(8 * i + 7, 8 * i)
    def word(i: Int): UInt = Cat(byte(i), byte(i + 1))
    def address(i: Int): UInt = Cat((i until i + 4).map(byte))
    val active = RegInit(false.B)
    val waiting = RegInit(false.B)
    val headerDone = RegInit(false.B)
    val count = RegInit(0.U(17.W))
    val headerLength = Reg(UInt(6.W))
    val totalLength = Reg(UInt(16.W))
    val source = Reg(UInt(32.W))
    val destination = Reg(UInt(32.W))
    val protocol = Reg(UInt(8.W))
    val local = Reg(UInt(32.W))
    val broadcast = Reg(Bool())
    val errors = RegInit(0.U(6.W))
    val offset = Mux(active, count, 0.U)
    val header = Mux(active, headerLength, Cat(byte(0)(3, 0), 0.U(2.W)))
    val total = Mux(active, totalLength, word(2))
    val nextCount = offset +& PopCount(io.in.bits.keep)
    val checksum = Module(new InternetChecksum(16))
    val headerLast = io.in.bits.last || (offset +& 16.U) >= header
    val headerKeep = VecInit((0 until 16).map(i => io.in.bits.keep(i) && (offset +& i.U) < header)).asUInt
    checksum.io.seed := 0.U
    checksum.io.in.bits := io.in.bits
    checksum.io.in.bits.keep := headerKeep
    checksum.io.in.bits.last := headerLast
    checksum.io.in.valid := io.in.valid && !waiting && !headerDone
    io.in.ready := !waiting && (headerDone || checksum.io.in.ready)

    val legalKeep = (io.in.bits.keep & (io.in.bits.keep + 1.U)) === 0.U
    val streamBad = !legalKeep || io.in.bits.keep === 0.U || io.in.bits.error ||
        (!io.in.bits.last && io.in.bits.keep =/= "hffff".U)
    // This is a destination host, not a forwarding router. RFC 1122 3.2.1.7
    // forbids rejecting an incoming datagram solely because TTL is below two.
    val firstBad = !active && (byte(0)(7, 4) =/= 4.U || header < 20.U || total < header ||
        word(6)(15) || (io.in.bits.keep & "hffff".U) =/= "hffff".U)
    val short = io.in.bits.last && (nextCount < total || nextCount < header)
    val fragment = !active && (word(6) & "h3fff".U) =/= 0.U
    val wrongDestination = active && offset === 16.U && address(0) =/= local &&
        !(broadcast && address(0) === "hffffffff".U)
    val unsupportedOptions = !active && header =/= 20.U
    val newErrors = Cat(unsupportedOptions, wrongDestination, fragment, false.B, firstBad || short, streamBad)
    when(io.in.fire) {
        when(!active) {
            headerLength := header
            totalLength := total
            source := address(12)
            destination := 0.U
            protocol := byte(9)
            local := io.localIp
            broadcast := io.acceptBroadcast
        }
        when(active && offset === 16.U) { destination := address(0) }
        errors := Mux(active, errors, 0.U) | newErrors
        count := Mux(nextCount > 131071.U, 131071.U, nextCount)
        active := !io.in.bits.last
        when(!headerDone && headerLast) { headerDone := true.B }
        when(io.in.bits.last) { waiting := true.B }
    }
    io.out.valid := waiting && checksum.io.out.valid
    io.out.bits.source := source
    io.out.bits.destination := destination
    io.out.bits.protocol := protocol
    io.out.bits.headerLength := headerLength
    io.out.bits.totalLength := totalLength
    io.out.bits.errors := errors | Mux(checksum.io.out.bits.checksum =/= 0.U, 4.U, 0.U)
    checksum.io.out.ready := waiting && io.out.ready
    when(io.out.fire) {
        waiting := false.B
        headerDone := false.B
    }
}
