package netstack

import chisel3._
import chisel3.util._

/** Round-robin arbitration locks a source as soon as its first beat is offered,
  * including when ready is low. No hidden queue: source last fire is wire last
  * fire, which is important for ARP retry timers.
  */
class PacketArbiter extends Module {
    val io = IO(new Bundle {
        val in = Vec(2, Flipped(Decoupled(new PacketBeat(16))))
        val out = Decoupled(new PacketBeat(16))
        val selected = Output(Bool())
    })
    val locked = RegInit(false.B)
    val owner = RegInit(false.B)
    val preferred = RegInit(false.B)
    val chosen = Mux(locked, owner, Mux(io.in(preferred).valid, preferred, !preferred))
    io.selected := chosen
    io.out.valid := io.in(chosen).valid
    io.out.bits := io.in(chosen).bits
    for (i <- 0 until 2) { io.in(i).ready := io.out.ready && chosen === (i == 1).B }
    when(io.out.valid && !locked) { owner := chosen; locked := true.B }
    when(io.out.fire && io.out.bits.last) { locked := false.B; preferred := !chosen }
}

/** Pad frames to 60 bytes, excluding the FCS supplied by the MAC. */
class EthernetPad extends Module {
    val io = IO(new Bundle {
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val out = Decoupled(new PacketBeat(16))
    })
    val padding = RegInit(false.B)
    val count = RegInit(0.U(17.W))
    val bad = RegInit(false.B)
    val incoming = PopCount(io.in.bits.keep)
    val deficit = Mux(count < 60.U, 60.U - count, 0.U)
    val needed = Mux(padding, deficit, Mux(io.in.bits.last && deficit > incoming, deficit, incoming))
    val emitted = Mux(needed > 16.U, 16.U, needed)(4, 0)
    val inputMask = VecInit((0 until 16).map(i => Fill(8, i.U < incoming))).asUInt
    io.in.ready := !padding && io.out.ready
    io.out.valid := padding || io.in.valid
    io.out.bits.data := Mux(padding, 0.U, io.in.bits.data & inputMask)
    io.out.bits.keep := ((1.U(17.W) << emitted) - 1.U)(15, 0)
    io.out.bits.last := (padding || io.in.bits.last) && needed <= 16.U
    io.out.bits.error := bad || (!padding && io.in.bits.error)
    when(io.out.fire) {
        val nextCount = count +& emitted
        count := Mux(nextCount > 131071.U, 131071.U, nextCount)
        bad := io.out.bits.error
        when(io.out.bits.last) { count := 0.U; bad := false.B; padding := false.B }
            .elsewhen(padding || io.in.bits.last) { padding := true.B }
    }
}

/** Classify the complete frame by its first beat, retaining selection to last. */
class EthernetDispatch extends Module {
    val io = IO(new Bundle {
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val localMac = Input(UInt(48.W))
        val ip = Decoupled(new PacketBeat(16))
        val arp = Decoupled(new PacketBeat(16))
        val dropped = Output(Bool())
    })
    val active = RegInit(false.B)
    val selection = RegInit(0.U(2.W))
    def field(i: Int, n: Int): UInt = Cat((i until i + n).map(j => io.in.bits.data(8 * j + 7, 8 * j)))
    val envelope = (io.in.bits.keep & "h3fff".U) === "h3fff".U &&
        (field(0, 6) === io.localMac || field(0, 6) === "hffffffffffff".U) &&
        field(6, 6) =/= 0.U && !io.in.bits.data(48)
    val first = Mux(!envelope, 0.U, Mux(field(12, 2) === "h0800".U, 1.U,
        Mux(field(12, 2) === "h0806".U, 2.U, 0.U)))
    val selected = Mux(active, selection, first)
    io.ip.bits := io.in.bits; io.arp.bits := io.in.bits
    io.ip.valid := io.in.valid && selected === 1.U
    io.arp.valid := io.in.valid && selected === 2.U
    io.in.ready := Mux(selected === 1.U, io.ip.ready, Mux(selected === 2.U, io.arp.ready, true.B))
    io.dropped := io.in.fire && io.in.bits.last && selected === 0.U
    when(io.in.fire) { selection := selected; active := !io.in.bits.last }
}

/** Commit a complete Ethernet IPv4 frame before stripping its 14-byte header. */
class EthernetIpv4Receive(val beatsPerPacket: Int = 1024) extends Module {
    val io = IO(new Bundle {
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val out = Decoupled(new PacketBeat(16))
        val dropped = Output(Bool())
        val linkBroadcast = Output(Bool())
    })
    val buffer = Module(new PacketCommitBuffer(16, beatsPerPacket))
    val metadata = Module(new MemoryQueue(new Bundle {
        val length = UInt(16.W); val broadcast = Bool()
    }, 2))
    val linkBroadcast = Reg(Bool())
    val slice = Module(new PacketSlice)
    val count = RegInit(0.U(17.W))
    val bad = RegInit(false.B)
    val pending = RegInit(false.B)
    io.in.ready := !pending && buffer.io.in.ready
    buffer.io.in.valid := !pending && io.in.valid
    buffer.io.in.bits := io.in.bits
    buffer.io.inContext := 0.U
    val nextCount = count +& PopCount(io.in.bits.keep)
    val legalKeep = (io.in.bits.keep & (io.in.bits.keep + 1.U)) === 0.U
    when(io.in.fire) {
        when(count === 0.U) { linkBroadcast := io.in.bits.data(0) }
        count := Mux(nextCount > 131071.U, 131071.U, nextCount)
        bad := bad || !legalKeep || io.in.bits.keep === 0.U || io.in.bits.error ||
            (!io.in.bits.last && io.in.bits.keep =/= "hffff".U) ||
            (io.in.bits.last && (nextCount < 34.U || nextCount > 65549.U))
        when(io.in.bits.last) { pending := true.B }
    }
    buffer.io.verdict.valid := pending && metadata.io.enq.ready
    buffer.io.verdict.bits := !bad
    metadata.io.enq.valid := pending && buffer.io.verdict.ready && !bad && !buffer.io.dropped
    metadata.io.enq.bits.length := (count - 14.U)(15, 0)
    metadata.io.enq.bits.broadcast := linkBroadcast
    when(buffer.io.verdict.fire) { count := 0.U; bad := false.B; pending := false.B }
    slice.io.config.valid := metadata.io.deq.valid
    metadata.io.deq.ready := slice.io.config.ready
    slice.io.config.bits.skip := 14.U
    slice.io.config.bits.length := metadata.io.deq.bits.length
    slice.io.config.bits.context := metadata.io.deq.bits.broadcast
    slice.io.in <> buffer.io.out
    io.out <> slice.io.out
    io.linkBroadcast := slice.io.context(0)
    io.dropped := buffer.io.dropped
}

/** Encapsulate a validated IP datagram, retaining it upstream while ARP resolves.
  * destinationIp is stable with the first input beat. Routes use one connected
  * subnet and an optional on-link gateway. VLAN and self-loopback are not handled.
  */
class EthernetIpv4Transmit extends Module {
    val io = IO(new Bundle {
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val destinationIp = Input(UInt(32.W))
        val localIp = Input(UInt(32.W)); val localMac = Input(UInt(48.W))
        val netmask = Input(UInt(32.W)); val gateway = Input(UInt(32.W)); val mtu = Input(UInt(16.W))
        val flush = Input(Bool())
        val resolve = Decoupled(UInt(32.W))
        val result = Flipped(Decoupled(new ArpResult))
        val out = Decoupled(new PacketBeat(16))
        val dropped = Output(Bool())
    })
    val idle :: query :: response :: configure :: sending :: draining :: Nil = Enum(6)
    val state = RegInit(idle)
    val nextHop = Reg(UInt(32.W))
    val sourceMac = Reg(UInt(48.W))
    val destinationMac = Reg(UInt(48.W))
    val length = Reg(UInt(16.W))
    val inputDone = RegInit(false.B)
    val prepend = Module(new PacketPrepend(14))
    val pad = Module(new EthernetPad)
    val destination = io.destinationIp
    val hostMask = ~io.netmask
    val maskValid = (hostMask & (hostMask + 1.U)) === 0.U
    val onLink = (destination & io.netmask) === (io.localIp & io.netmask)
    val broadcast = destination === "hffffffff".U ||
        (onLink && hostMask > 1.U && (destination | io.netmask) === "hffffffff".U)
    val multicast = destination(31, 28) === 14.U
    val gatewayValid = io.gateway =/= 0.U && io.gateway =/= io.localIp &&
        io.gateway(31, 28) < 14.U && io.gateway(31, 24) =/= 127.U &&
        (io.gateway & io.netmask) === (io.localIp & io.netmask) &&
        !(hostMask > 1.U && (io.gateway | io.netmask) === "hffffffff".U)
    val direct = broadcast || multicast
    val incomingLength = Cat(io.in.bits.data(23, 16), io.in.bits.data(31, 24))
    when(state === idle && io.in.valid && !io.flush) {
        sourceMac := io.localMac
        length := incomingLength
        inputDone := false.B
        nextHop := Mux(onLink, destination, io.gateway)
        when(!maskValid || incomingLength < 20.U || incomingLength > io.mtu || destination === io.localIp ||
            destination(31, 24) === 0.U || destination(31, 24) === 127.U ||
            (destination(31, 28) === 15.U && !broadcast) || (!direct && !onLink && !gatewayValid)) {
            state := draining
        }.elsewhen(direct) {
            destinationMac := Mux(broadcast, "hffffffffffff".U, Cat("h01005e".U(24.W), 0.U(1.W), destination(22, 0)))
            state := configure
        }.otherwise { state := query }
    }
    io.resolve.valid := state === query && !io.flush
    io.resolve.bits := nextHop
    io.result.ready := state === response
    when(io.resolve.fire) { state := response }
    when(io.result.fire) {
        destinationMac := io.result.bits.mac
        state := Mux(io.result.bits.error, draining, configure)
    }
    val header = Wire(Vec(14, UInt(8.W)))
    for (i <- 0 until 6) {
        header(i) := destinationMac(47 - 8 * i, 40 - 8 * i)
        header(i + 6) := sourceMac(47 - 8 * i, 40 - 8 * i)
    }
    header(12) := 8.U; header(13) := 0.U
    prepend.io.config.valid := state === configure && !io.flush
    // Explicit wide placement avoids GSIM 93b8cd2 emitting a chain of uint64_t
    // shifts for a constant source MAC/EtherType followed by a dynamic MAC.
    // A byte-vector Cat is correct RTL, but that model truncates its upper bits.
    val destinationWire = VecInit((0 until 6).map(header(_))).asUInt
    val sourceWire = VecInit((6 until 12).map(header(_))).asUInt
    prepend.io.config.bits.header := destinationWire.pad(112) |
        (sourceWire.pad(112) << 48)(111, 0) | (8.U(112.W) << 96)(111, 0)
    prepend.io.config.bits.length := length
    prepend.io.config.bits.context := 0.U
    when(prepend.io.config.fire) { state := sending }
    prepend.io.in.valid := state === sending && !inputDone && io.in.valid
    prepend.io.in.bits := io.in.bits
    io.in.ready := state === draining || (state === sending && !inputDone && prepend.io.in.ready)
    pad.io.in <> prepend.io.out
    io.out <> pad.io.out
    io.dropped := state === draining && io.in.fire && io.in.bits.last
    when(state === sending && io.in.fire && io.in.bits.last) { inputDone := true.B }
    when(io.out.fire && io.out.bits.last) { state := idle }
    when(io.dropped) { state := idle }
    when(io.flush && (state === query || state === configure)) { state := draining }
}
