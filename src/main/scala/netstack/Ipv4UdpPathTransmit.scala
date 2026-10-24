package netstack

import chisel3._
import chisel3.util._

class UdpPathNotice extends Bundle {
    val destinationIp = UInt(32.W)
    val sourcePort = UInt(16.W); val destinationPort = UInt(16.W)
    val length = UInt(16.W); val mtu = UInt(16.W)
    // A local length rejection is distinguished from a peer/router ICMP error.
    val local = Bool(); val icmpType = UInt(8.W); val icmpCode = UInt(8.W)
    val routerIp = UInt(32.W)
}
class UdpSentQuote extends Bundle {
    val sourceIp = UInt(32.W); val destinationIp = UInt(32.W)
    val sourcePort = UInt(16.W); val destinationPort = UInt(16.W)
    val identification = UInt(16.W); val totalLength = UInt(16.W)
    val tail = UInt(32.W) // UDP length and checksum: the other four quoted bytes.
}

/** UDP PMTU is an application-visible datagram limit, not fragmentation.
  * Cache updates require an exact recent routed datagram quote, including IP
  * identification, UDP length/checksum and the four-tuple. ICMP remains
  * advisory: no parser can authenticate the router from an eight-byte quote.
  * One ordered routed result is required for each IP packet (success=true).
  * Route acceptance is not a physical wire completion; UDP has no TCP ledger.
  */
class Ipv4UdpPathTransmit(val beatsPerPacket: Int = 128, val ticksPerSecond: Int = 1000,
    val entries: Int = 4, val historyDepth: Int = 8) extends Module {
    require(isPow2(entries) && isPow2(historyDepth))
    val io = IO(new Bundle {
        val command = Flipped(Decoupled(new UdpSendCommand))
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val out = Decoupled(new PacketBeat(16))
        val destinationIp = Output(UInt(32.W)); val dropped = Output(Bool())
        val mtu = Input(UInt(16.W)); val tick = Input(Bool()); val flush = Input(Bool())
        val error = Flipped(Valid(new IcmpErrorEvent))
        val routed = Flipped(Valid(Bool()))
        val notice = Valid(new UdpPathNotice)
    })
    val tx = Module(new Ipv4UdpTransmit(beatsPerPacket))
    val cacheValid = RegInit(VecInit(Seq.fill(entries)(false.B)))
    val cacheIp = Reg(Vec(entries, UInt(32.W)))
    val cacheMtu = Reg(Vec(entries, UInt(16.W)))
    val cacheAge = RegInit(VecInit(Seq.fill(entries)(0.U(log2Ceil(ticksPerSecond.toLong * 600 + 1).W))))
    val nextCache = RegInit(0.U(log2Ceil(entries).W))
    def ceiling(ip: UInt): UInt = {
        val matches = VecInit((0 until entries).map(i => cacheValid(i) && cacheIp(i) === ip))
        val learned = Mux1H(matches, cacheMtu)
        Mux(matches.asUInt.orR && learned < io.mtu, learned, io.mtu)
    }
    val rejecting = RegInit(false.B)
    val rejected = Reg(new UdpSendCommand); val rejectedMtu = Reg(UInt(16.W))
    val selectedMtu = ceiling(io.command.bits.destinationIp)
    val tooLarge = (io.command.bits.length +& 28.U) > selectedMtu
    io.command.ready := !rejecting && tx.io.command.ready && !io.flush
    tx.io.command.valid := io.command.valid && !rejecting && !tooLarge && !io.flush
    tx.io.command.bits := io.command.bits
    when(io.command.fire && tooLarge) {
        rejecting := true.B; rejected := io.command.bits; rejectedMtu := selectedMtu
    }
    tx.io.in.valid := io.in.valid && !rejecting
    tx.io.in.bits := io.in.bits
    io.in.ready := Mux(rejecting, true.B, tx.io.in.ready)
    val localDrop = rejecting && io.in.fire && io.in.bits.last
    when(localDrop) { rejecting := false.B }
    io.destinationIp := tx.io.destinationIp
    io.dropped := tx.io.dropped || localDrop

    // Capture exact serialized headers. History is committed only on successful
    // routing, not on command acceptance, failed checksum or an ARP/MTU drop.
    val beat = RegInit(0.U(2.W))
    val pending = Reg(new UdpSentQuote)
    val submitted = RegInit(false.B)
    io.out.valid := tx.io.out.valid && !submitted
    io.out.bits := tx.io.out.bits
    tx.io.out.ready := io.out.ready && !submitted
    def byte(i: Int): UInt = tx.io.out.bits.data(8 * i + 7, 8 * i)
    def word(i: Int): UInt = Cat(byte(i), byte(i + 1))
    def address(i: Int): UInt = Cat((i until i + 4).map(byte))
    when(tx.io.out.fire) {
        when(beat === 0.U) {
            pending.sourceIp := address(12)
            pending.identification := word(4); pending.totalLength := word(2)
        }
        when(beat === 1.U) {
            pending.destinationIp := address(0)
            pending.sourcePort := word(4); pending.destinationPort := word(6)
            pending.tail := address(8)
        }
        when(beat < 2.U) { beat := beat + 1.U }
        when(tx.io.out.bits.last) { beat := 0.U; submitted := true.B }
    }
    val history = Reg(Vec(historyDepth, new UdpSentQuote))
    val historyValid = RegInit(VecInit(Seq.fill(historyDepth)(false.B)))
    val historyAge = RegInit(VecInit(Seq.fill(historyDepth)(0.U(log2Ceil(ticksPerSecond.toLong * 60 + 1).W))))
    val nextHistory = RegInit(0.U(log2Ceil(historyDepth).W))
    val quote = io.error.bits
    val hits = (0 until historyDepth).map { i =>
        historyValid(i) && quote.sourceIp === history(i).sourceIp &&
            quote.destinationIp === history(i).destinationIp &&
            quote.sourcePort === history(i).sourcePort && quote.destinationPort === history(i).destinationPort &&
            quote.identification === history(i).identification && quote.totalLength === history(i).totalLength &&
            quote.sequence === history(i).tail
    }
    val candidate = RegNext(io.error.valid && quote.protocol === 17.U && hits.reduce(_ || _) && !io.flush, false.B)
    val event = RegEnable(quote, io.error.valid)
    val currentMtu = ceiling(event.destinationIp)
    val reduced = PathMtuRules.decreased(event.nextHopMtu, event.totalLength, currentMtu)
    val tooBig = event.icmpType === 3.U && event.icmpCode === 4.U
    val decrease = candidate && tooBig && event.dontFragment && reduced >= 68.U &&
        reduced < event.totalLength && reduced < currentMtu && !io.flush
    val cacheHits = VecInit((0 until entries).map(i => cacheValid(i) && cacheIp(i) === event.destinationIp))
    val slot = Mux(cacheHits.asUInt.orR, PriorityEncoder(cacheHits.asUInt), nextCache)
    for (i <- 0 until entries) {
        when(io.tick && cacheValid(i)) {
            when(cacheAge(i) === (ticksPerSecond.toLong * 600 - 1).U) { cacheValid(i) := false.B }
                .otherwise { cacheAge(i) := cacheAge(i) + 1.U }
        }
    }
    for (i <- 0 until historyDepth) {
        when(io.tick && historyValid(i)) {
            when(historyAge(i) === (ticksPerSecond.toLong * 60 - 1).U) { historyValid(i) := false.B }
                .otherwise { historyAge(i) := historyAge(i) + 1.U }
        }
    }
    when(io.routed.valid && submitted) {
        submitted := false.B
        when(io.routed.bits && !io.flush) {
            history(nextHistory) := pending; historyValid(nextHistory) := true.B
            historyAge(nextHistory) := 0.U; nextHistory := nextHistory + 1.U
        }
    }
    when(decrease) {
        cacheValid(slot) := true.B; cacheIp(slot) := event.destinationIp
        cacheMtu(slot) := reduced; cacheAge(slot) := 0.U
        when(!cacheHits.asUInt.orR) { nextCache := nextCache + 1.U }
    }
    when(io.flush) {
        cacheValid.foreach(_ := false.B); historyValid.foreach(_ := false.B)
        submitted := false.B
    }
    // Pulsed advisory interface: consumers latch notices; no error queue can
    // backpressure an unrelated TCP connection. Local errors take precedence.
    io.notice.valid := RegNext(localDrop || (candidate && (!tooBig || decrease) && !io.flush), false.B) && !io.flush
    val notice = Reg(new UdpPathNotice)
    when(candidate && !io.flush) {
        notice.local := false.B; notice.destinationIp := event.destinationIp
        notice.sourcePort := event.sourcePort; notice.destinationPort := event.destinationPort
        notice.length := event.totalLength - 28.U; notice.mtu := Mux(decrease, reduced, currentMtu)
        notice.icmpType := event.icmpType; notice.icmpCode := event.icmpCode; notice.routerIp := event.routerIp
    }
    when(localDrop) {
        notice.local := true.B; notice.destinationIp := rejected.destinationIp
        notice.sourcePort := rejected.sourcePort; notice.destinationPort := rejected.destinationPort
        notice.length := rejected.length; notice.mtu := rejectedMtu
        notice.icmpType := 0.U; notice.icmpCode := 0.U; notice.routerIp := 0.U
    }
    io.notice.bits := notice
}
