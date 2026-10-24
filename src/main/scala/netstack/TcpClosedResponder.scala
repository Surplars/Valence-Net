package netstack

import chisel3._
import chisel3.util._

/** Rate-limited CLOSED-state TCP responses. Only packets for which the caller
  * has established that no TCB owns the tuple may enter this block.
  *
  * Input is deliberately always ready: admission is all-or-nothing at the
  * first beat, and a busy validator drops the entire next frame. Once admitted,
  * Ipv4Receive accepts every input beat through last (including oversize/error
  * frames), then TcpValidate checks the complete transport checksum/options.
  * A blocked response stream never stalls shared ingress or another connection.
  * The caller must suppress link-layer broadcast/multicast before this boundary.
  * Configuration is fixed while active. These stateless packets have no
  * connection completion-ledger ownership.
  */
class TcpClosedResponder(val packetBeats: Int = 128, val intervalTicks: Int = 10) extends Module {
    require(intervalTicks >= 1)
    val io = IO(new Bundle {
        val localIp = Input(UInt(32.W)); val netmask = Input(UInt(32.W))
        val tick = Input(Bool())
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val out = Decoupled(new PacketBeat(16))
        val destinationIp = Output(UInt(32.W))
        val busy = Output(Bool())
        val dropped = Output(Bool())
        val generated = Output(Bool())
    })
    val ip = Module(new Ipv4Receive(packetBeats))
    val tcp = Module(new TcpValidate)
    val first = RegInit(true.B)
    val admitted = RegInit(false.B)
    val validating = RegInit(false.B)
    val choose = Mux(first, !validating && ip.io.in.ready, admitted)
    io.in.ready := true.B
    ip.io.in.valid := io.in.valid && choose
    ip.io.in.bits := io.in.bits
    ip.io.localIp := io.localIp; ip.io.acceptBroadcast := false.B
    when(io.in.fire) {
        first := io.in.bits.last
        when(first) { admitted := choose; when(choose) { validating := true.B } }
        // Ipv4Receive has no inter-beat backpressure for a newly admitted frame.
        // It drains overflow to last and rejects it, preserving stream alignment.
        when(choose) { assert(ip.io.in.ready) }
    }
    val selected = ip.io.protocol === 6.U
    tcp.io.in.valid := ip.io.out.valid && selected
    tcp.io.in.bits := ip.io.out.bits
    ip.io.out.ready := !selected || tcp.io.in.ready
    tcp.io.sourceIp := ip.io.source; tcp.io.destinationIp := ip.io.destination
    tcp.io.ipPayloadLength := ip.io.payloadLength
    tcp.io.out.ready := true.B
    when(ip.io.dropped || (ip.io.out.fire && ip.io.out.bits.last && !selected) || tcp.io.out.fire) {
        validating := false.B
    }

    val response = Module(new TcpResetTransmit(intervalTicks))
    response.io.localIp := io.localIp; response.io.netmask := io.netmask; response.io.tick := io.tick
    response.io.in.valid := tcp.io.out.valid; response.io.in.bits := tcp.io.out.bits
    io.out <> response.io.out; io.destinationIp := response.io.destinationIp
    io.busy := validating; io.generated := response.io.generated
    io.dropped := (io.in.fire && io.in.bits.last && !choose) || ip.io.dropped ||
        (ip.io.out.fire && ip.io.out.bits.last && !selected) || response.io.dropped
}

/** CLOSED/LISTEN-invalid-ACK response generator for already validated metadata.
  * The caller authorizes tuple ownership and drains the corresponding payload.
  * Metadata is always consumed; excess/rate-limited responses are discarded.
  */
class TcpResetTransmit(val intervalTicks: Int = 10) extends Module {
    require(intervalTicks >= 1)
    val io = IO(new Bundle {
        val localIp = Input(UInt(32.W)); val netmask = Input(UInt(32.W)); val tick = Input(Bool())
        val in = Flipped(Decoupled(new TcpSegment))
        val out = Decoupled(new PacketBeat(16))
        val destinationIp = Output(UInt(32.W))
        val generated = Output(Bool()); val dropped = Output(Bool())
    })
    io.in.ready := true.B
    val serializer = Module(new Ipv4TcpTransmit(2))
    def unicast(address: UInt): Bool =
        address(31, 28) < 14.U && address(31, 24) =/= 0.U && address(31, 24) =/= 127.U
    // /31 and /32 have no directed broadcast host address.
    def directedBroadcast(address: UInt): Bool =
        io.netmask =/= 0.U && io.netmask < "hfffffffe".U &&
            (address & io.netmask) === (io.localIp & io.netmask) &&
            (address | io.netmask).andR
    val segment = io.in.bits
    val acceptable = segment.errors === 0.U && segment.destinationIp === io.localIp && !segment.flags(2) &&
        unicast(segment.sourceIp) && unicast(segment.destinationIp) &&
        !directedBroadcast(segment.sourceIp) && !directedBroadcast(segment.destinationIp)
    val pending = RegInit(false.B)
    val reply = Reg(new TcpSendCommand)
    val identification = RegInit(0.U(16.W))
    val cooldown = RegInit(0.U(math.max(1, log2Ceil(intervalTicks + 1)).W))
    when(io.tick && cooldown =/= 0.U) { cooldown := cooldown - 1.U }
    val produce = io.in.fire && acceptable && !pending && cooldown === 0.U
    when(produce) {
        pending := true.B
        cooldown := intervalTicks.U
        reply := 0.U.asTypeOf(new TcpSendCommand)
        reply.sourceIp := segment.destinationIp; reply.destinationIp := segment.sourceIp
        reply.sourcePort := segment.destinationPort; reply.destinationPort := segment.sourcePort
        reply.sequence := Mux(segment.flags(4), segment.acknowledgement, 0.U)
        reply.acknowledgement := Mux(segment.flags(4), 0.U,
            segment.sequence + segment.payloadLength + segment.flags(1).asUInt + segment.flags(0).asUInt)
        reply.flags := Mux(segment.flags(4), 4.U, 20.U)
        reply.ttl := 64.U
        reply.identification := identification
        identification := identification + 1.U
    }
    val emptyPayload = RegInit(false.B)
    serializer.io.command.valid := pending && !emptyPayload
    serializer.io.command.bits := reply
    when(serializer.io.command.fire) { pending := false.B; emptyPayload := true.B }
    serializer.io.in.valid := emptyPayload
    serializer.io.in.bits := 0.U.asTypeOf(new PacketBeat(16))
    serializer.io.in.bits.last := true.B
    when(serializer.io.in.fire) { emptyPayload := false.B }
    io.out <> serializer.io.out
    io.destinationIp := serializer.io.destinationIp
    io.generated := produce
    io.dropped := (io.in.fire && !produce) || serializer.io.dropped
}

class TcpClosedHarness extends Module {
    val io = IO(new Bundle {
        val localIp = Input(UInt(32.W)); val netmask = Input(UInt(32.W)); val tick = Input(Bool())
        val lo = Input(UInt(64.W)); val hi = Input(UInt(64.W))
        val keep = Input(UInt(16.W)); val last = Input(Bool()); val error = Input(Bool())
        val valid = Input(Bool()); val ready = Output(Bool())
        val outReady = Input(Bool()); val outValid = Output(Bool())
        val outLo = Output(UInt(64.W)); val outHi = Output(UInt(64.W))
        val outKeep = Output(UInt(16.W)); val outLast = Output(Bool()); val outError = Output(Bool())
        val destination = Output(UInt(32.W))
        val busy = Output(Bool()); val dropped = Output(Bool()); val generated = Output(Bool())
    })
    val dut = Module(new TcpClosedResponder(32, intervalTicks = 2))
    dut.io.localIp := io.localIp; dut.io.netmask := io.netmask; dut.io.tick := io.tick
    dut.io.in.valid := io.valid; io.ready := dut.io.in.ready
    dut.io.in.bits.data := Cat(io.hi, io.lo); dut.io.in.bits.keep := io.keep
    dut.io.in.bits.last := io.last; dut.io.in.bits.error := io.error
    dut.io.out.ready := io.outReady; io.outValid := dut.io.out.valid
    io.outLo := dut.io.out.bits.data(63, 0); io.outHi := dut.io.out.bits.data(127, 64)
    io.outKeep := dut.io.out.bits.keep; io.outLast := dut.io.out.bits.last; io.outError := dut.io.out.bits.error
    io.destination := dut.io.destinationIp
    io.busy := dut.io.busy; io.dropped := dut.io.dropped; io.generated := dut.io.generated
}
