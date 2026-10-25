package netstack

import chisel3._
import chisel3.util._

/** Validated quotation of a locally originated IPv4 TCP/UDP datagram.
  * This is advisory until the transport verifies tuple and outstanding sequence
  * (or UDP recent-send identity). The router need not be the remote peer.
  */
class IcmpErrorEvent extends Bundle {
    val routerIp = UInt(32.W)
    val sourceIp = UInt(32.W); val destinationIp = UInt(32.W)
    val protocol = UInt(8.W)
    val sourcePort = UInt(16.W); val destinationPort = UInt(16.W)
    val sequence = UInt(32.W)
    val totalLength = UInt(16.W); val identification = UInt(16.W)
    val dontFragment = Bool(); val nextHopMtu = UInt(16.W)
    val icmpType = UInt(8.W); val icmpCode = UInt(8.W); val pointer = UInt(8.W)
}

object IcmpAddress {
    def unicast(address: UInt): Bool =
        address(31, 24) =/= 0.U && address(31, 24) =/= 127.U && address(31, 28) < 14.U
    // /31 point-to-point and /32 host routes do not have directed broadcast.
    def directedBroadcast(address: UInt, mask: UInt): Bool = {
        val host = ~mask
        host > 1.U && (address & host) === host
    }
    def localBroadcast(address: UInt, local: UInt, mask: UInt): Bool =
        directedBroadcast(address, mask) && (address & mask) === (local & mask)
}

/** Full-packet ICMP error validation, with a bounded advisory event queue.
  * Outer IPv4 is validated before payload parsing; no event is published until
  * the entire ICMP checksum is valid. Quotes use IHL=5, matching local output.
  * Receive event congestion drops advisory errors rather than blocking input.
  * Echo/informational ICMP types are ignored; no Source Quench/Redirect policy.
  */
class Ipv4IcmpErrorReceive(val beatsPerPacket: Int = 128) extends Module {
    val io = IO(new Bundle {
        val localIp = Input(UInt(32.W)); val netmask = Input(UInt(32.W))
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val event = Decoupled(new IcmpErrorEvent)
        val dropped = Output(UInt(2.W))
    })
    val ip = Module(new Ipv4Receive(beatsPerPacket))
    val checksum = Module(new InternetChecksum(16))
    val events = Module(new MemoryQueue(new IcmpErrorEvent, 2))
    ip.io.in <> io.in; ip.io.localIp := io.localIp; ip.io.acceptBroadcast := false.B
    checksum.io.in <> ip.io.out; checksum.io.seed := 0.U
    checksum.io.out.ready := true.B
    io.event <> events.io.deq

    val prefix = Reg(Vec(36, UInt(8.W)))
    val beat = RegInit(0.U(2.W))
    val packetGood = RegInit(false.B)
    val router = Reg(UInt(32.W))
    val local = Reg(UInt(32.W))
    val mask = Reg(UInt(32.W))
    def byte(i: Int): UInt = prefix(i)
    def word(i: Int): UInt = Cat(byte(i), byte(i + 1))
    def address(i: Int): UInt = Cat((i until i + 4).map(byte))
    when(ip.io.out.fire) {
        for (i <- 0 until 36) {
            when(beat === (i / 16).U) { prefix(i) := ip.io.out.bits.data(8 * (i % 16) + 7, 8 * (i % 16)) }
        }
        when(beat === 0.U) {
            packetGood := ip.io.protocol === 1.U && ip.io.payloadLength >= 36.U &&
                IcmpAddress.unicast(ip.io.source)
            router := ip.io.source; local := ip.io.destination; mask := io.netmask
        }
        when(ip.io.out.bits.last) { beat := 0.U }
            .elsewhen(beat =/= 3.U) { beat := beat + 1.U }
    }
    val kind = byte(0); val code = byte(1)
    val supported = (kind === 3.U && code <= 15.U) ||
        (kind === 11.U && code <= 1.U) || (kind === 12.U && code <= 2.U)
    val quotedChecksum = OnesComplement.sum16((8 until 28 by 2).map(word))
    val transport = byte(17)
    val quotedLength = word(10)
    val quotedGood = byte(8) === "h45".U && quotedChecksum === "hffff".U &&
        (word(14) & "hbfff".U) === 0.U && address(20) === local &&
        IcmpAddress.unicast(address(24)) &&
        !IcmpAddress.localBroadcast(router, local, mask) &&
        !IcmpAddress.localBroadcast(address(24), local, mask) &&
        ((transport === 6.U && quotedLength >= 40.U) ||
            (transport === 17.U && quotedLength >= 28.U && word(32) === quotedLength - 20.U))
    val acceptable = packetGood && supported && quotedGood &&
        !checksum.io.out.bits.error && checksum.io.out.bits.checksum === 0.U
    events.io.enq.valid := checksum.io.out.valid && acceptable
    val e = events.io.enq.bits
    e.routerIp := router
    e.sourceIp := address(20); e.destinationIp := address(24)
    e.protocol := transport; e.sourcePort := word(28); e.destinationPort := word(30)
    e.sequence := address(32)
    e.totalLength := quotedLength; e.identification := word(12); e.dontFragment := byte(14)(6)
    e.nextHopMtu := Mux(kind === 3.U && code === 4.U, word(6), 0.U)
    e.icmpType := kind; e.icmpCode := code; e.pointer := Mux(kind === 12.U, byte(4), 0.U)
    val isError = kind === 3.U || kind === 11.U || kind === 12.U
    io.dropped := PopCount(VecInit(Seq(ip.io.dropped,
        checksum.io.out.valid && isError && (!acceptable || !events.io.enq.ready))))
}

class IcmpErrorReply extends Bundle {
    val source = UInt(32.W); val destination = UInt(32.W)
    val code = UInt(8.W); val quoteLength = UInt(6.W)
    val quote = Vec(28, UInt(8.W))
}

/** Validate a rejected complete IPv4 datagram and generate destination
  * unreachable: code 2 (protocol) or code 3 (UDP port).
  * A code-3 request additionally validates UDP length and checksum. Only the
  * original header and up to eight payload bytes are retained, never padding.
  * All fragments/options are outside this endpoint's existing IPv4 scope.
  * Link-layer broadcast suppression belongs to the Ethernet ingress adapter.
  * One token per period (100 errors/s at the default 1 kHz tick), two queued
  * requests: output backpressure never prevents draining a rejected datagram.
  */
class Ipv4IcmpErrorTransmit(val ticksPerSecond: Int = 1000) extends Module {
    require(ticksPerSecond > 0)
    private val period = math.max(1, ticksPerSecond / 100)
    val io = IO(new Bundle {
        val localIp = Input(UInt(32.W)); val netmask = Input(UInt(32.W))
        val code = Input(UInt(8.W)); val tick = Input(Bool())
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val out = Decoupled(new PacketBeat(16))
        val destinationIp = Output(UInt(32.W))
        val dropped = Output(Bool())
        val suppressed = Output(Bool())
    })
    val validator = Module(new Ipv4Validate)
    validator.io.in <> io.in; validator.io.localIp := io.localIp; validator.io.acceptBroadcast := false.B
    validator.io.out.ready := true.B
    val requests = Module(new MemoryQueue(new IcmpErrorReply, 2))
    val prefix = Reg(Vec(28, UInt(8.W)))
    val receiving = RegInit(false.B)
    val offset = RegInit(0.U(17.W))
    val declared = Reg(UInt(16.W))
    val code = Reg(UInt(8.W))
    val mask = Reg(UInt(32.W))
    val sum = RegInit(0.U(16.W))
    val position = Mux(receiving, offset, 0.U)
    def byte(i: Int): UInt = prefix(i)
    def word(i: Int): UInt = Cat(byte(i), byte(i + 1))
    def address(i: Int): UInt = Cat((i until i + 4).map(byte))
    // Every offset is even; excluding the 20-byte header retains word parity.
    // Mask with IP total length so Ethernet padding cannot change UDP checksum.
    val words = (0 until 8).map { pair =>
        def octet(i: Int): UInt = {
            val at = position +& i.U
            Mux(position =/= 0.U && at >= 20.U && at < declared && io.in.bits.keep(i),
                io.in.bits.data(8 * i + 7, 8 * i), 0.U(8.W))
        }
        Cat(octet(2 * pair), octet(2 * pair + 1))
    }
    val nextSum = OnesComplement.sum16(words :+ Mux(receiving, sum, 0.U))
    when(io.in.fire) {
        for (i <- 0 until 28) {
            when(position === ((i / 16) * 16).U) {
                prefix(i) := io.in.bits.data(8 * (i % 16) + 7, 8 * (i % 16))
            }
        }
        when(!receiving) {
            declared := Cat(io.in.bits.data(23, 16), io.in.bits.data(31, 24))
            code := io.code; mask := io.netmask
        }
        sum := nextSum
        val nextOffset = position +& 16.U
        offset := Mux(nextOffset > 131071.U, 131071.U, nextOffset)
        receiving := !io.in.bits.last
    }
    val v = validator.io.out.bits
    val udpLength = word(24)
    val udpSum = OnesComplement.sum16(Seq(sum, v.source(31, 16), v.source(15, 0),
        v.destination(31, 16), v.destination(15, 0), udpLength, 17.U(16.W)))
    val udpGood = v.protocol === 17.U && v.totalLength >= 28.U &&
        udpLength >= 8.U && udpLength === v.totalLength - 20.U &&
        (word(26) === 0.U || udpSum === "hffff".U)
    val originalError = v.protocol === 1.U && v.totalLength > 20.U &&
        (byte(20) === 3.U || byte(20) === 4.U || byte(20) === 5.U ||
            byte(20) === 11.U || byte(20) === 12.U)
    val eligible = v.errors === 0.U &&
        IcmpAddress.unicast(v.source) && IcmpAddress.unicast(v.destination) &&
        !IcmpAddress.localBroadcast(v.source, v.destination, mask) &&
        !IcmpAddress.directedBroadcast(v.destination, mask) && !originalError &&
        ((code === 2.U && v.protocol =/= 1.U && v.protocol =/= 6.U && v.protocol =/= 17.U) ||
            (code === 3.U && udpGood))
    val remaining = RegInit(0.U(log2Ceil(period + 1).W))
    when(io.tick && remaining =/= 0.U) { remaining := remaining - 1.U }
    requests.io.enq.valid := validator.io.out.valid && eligible && remaining === 0.U
    requests.io.enq.bits.source := v.destination
    requests.io.enq.bits.destination := v.source
    requests.io.enq.bits.code := code
    requests.io.enq.bits.quoteLength := Mux(v.totalLength < 28.U, v.totalLength, 28.U)
    for (i <- 0 until 28) {
        requests.io.enq.bits.quote(i) := Mux(i.U < v.totalLength, prefix(i), 0.U)
    }
    when(requests.io.enq.fire) { remaining := period.U }
    io.dropped := validator.io.out.valid && !eligible
    io.suppressed := validator.io.out.valid && eligible &&
        (remaining =/= 0.U || !requests.io.enq.ready)

    val sending = RegInit(false.B)
    val frame = Reg(Vec(56, UInt(8.W)))
    val length = Reg(UInt(6.W))
    val destination = Reg(UInt(32.W))
    val beat = RegInit(0.U(2.W))
    val identification = RegInit(0.U(16.W))
    val tx = requests.io.deq.bits
    val bytes = Wire(Vec(56, UInt(8.W)))
    bytes.foreach(_ := 0.U)
    def put16(i: Int, value: UInt): Unit = {
        bytes(i) := value(15, 8); bytes(i + 1) := value(7, 0)
    }
    val total = 28.U(16.W) + tx.quoteLength
    bytes(0) := "h45".U; put16(2, total); put16(4, identification)
    put16(6, "h4000".U(16.W)); bytes(8) := 64.U; bytes(9) := 1.U
    put16(12, tx.source(31, 16)); put16(14, tx.source(15, 0))
    put16(16, tx.destination(31, 16)); put16(18, tx.destination(15, 0))
    val headerSum = OnesComplement.sum16(Seq("h4500".U(16.W), total, identification,
        "h4000".U(16.W), "h4001".U(16.W), tx.source(31, 16), tx.source(15, 0),
        tx.destination(31, 16), tx.destination(15, 0)))
    put16(10, ~headerSum)
    bytes(20) := 3.U; bytes(21) := tx.code
    for (i <- 0 until 28) { bytes(28 + i) := tx.quote(i) }
    val errorSum = OnesComplement.sum16(Seq(Cat(3.U(8.W), tx.code)) ++
        (0 until 28 by 2).map(i => Cat(tx.quote(i), tx.quote(i + 1))))
    put16(22, ~errorSum)
    requests.io.deq.ready := !sending
    when(requests.io.deq.fire) {
        frame := bytes; length := total; destination := tx.destination
        sending := true.B; beat := 0.U; identification := identification + 1.U
    }
    val chunks = (0 until 4).map { index =>
        Cat((0 until 16).reverse.map { i =>
            if (index * 16 + i < 56) frame(index * 16 + i) else 0.U(8.W)
        })
    }
    io.out.valid := sending
    io.out.bits.data := VecInit(chunks)(beat)
    val txOffset = Cat(beat, 0.U(4.W))
    io.out.bits.keep := VecInit((0 until 16).map(i => txOffset +& i.U < length)).asUInt
    io.out.bits.last := (txOffset +& 16.U) >= length
    io.out.bits.error := false.B
    io.destinationIp := destination
    when(io.out.fire) {
        when(io.out.bits.last) { sending := false.B }
            .otherwise { beat := beat + 1.U }
    }
}
