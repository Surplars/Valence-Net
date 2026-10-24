package netstack

import chisel3._
import chisel3.util._

class TcpPathTuple extends Bundle {
    val localIp = UInt(32.W); val remoteIp = UInt(32.W)
    val localPort = UInt(16.W); val remotePort = UInt(16.W)
}

/** Advisory errors never implicitly destroy a live connection. */
class TcpNetworkError extends Bundle {
    val generation = UInt(32.W); val routerIp = UInt(32.W)
    val icmpType = UInt(8.W); val icmpCode = UInt(8.W); val pointer = UInt(8.W)
    val sequence = UInt(32.W); val mtu = UInt(16.W)
}

object PathMtuRules {
    /** RFC 1191 plateaus for zero-MTU reports, anchored to the quoted datagram.
      * Callers independently require a strict decrease from current PMTU, so
      * duplicate/stale reports cannot walk an unchanged packet down the table.
      */
    def decreased(reported: UInt, quotedLength: UInt, current: UInt): UInt = {
        val bound = quotedLength
        val plateaus = Seq(32000, 17914, 8166, 4352, 2002, 1492, 1006, 508, 296, 68)
        val fallback = PriorityMux(plateaus.map(n => (bound > n.U) -> n.U(16.W)) :+
            (true.B -> 68.U(16.W)))
        Mux(reported === 0.U, fallback, reported)
    }
}

/** Cold, registered validation of a checksummed ICMP quote against a live TCB.
  * Only physically completed, unacknowledged sequence space is eligible.
  * Four-tuple and generation checks are repeated across the register boundary.
  * The 10 minute idle-only expiry permits future larger packets, never replay
  * in response to an upward change. No ICMP black-hole detection is implied.
  */
class TcpIcmpFeedback(val ticksPerSecond: Int = 1000, val expirySeconds: Int = 600) extends Module {
    require(ticksPerSecond > 0 && expirySeconds >= 300)
    private val expiry = ticksPerSecond.toLong * expirySeconds
    require(expiry < (1L << 32))
    val io = IO(new Bundle {
        val error = Flipped(Valid(new IcmpErrorEvent))
        val tuple = Input(new TcpPathTuple)
        val state = Input(UInt(4.W)); val generation = Input(UInt(32.W))
        val oldest = Input(UInt(32.W)); val actual = Input(UInt(32.W)); val reserved = Input(UInt(32.W))
        val pathMtu = Input(UInt(16.W)); val limit = Input(UInt(16.W))
        val tick = Input(Bool()); val clear = Input(Bool())
        val update = Valid(new TcpPathMtuUpdate)
        val notice = Valid(new TcpNetworkError)
    })
    val active = io.state >= TcpState.synSent.U && io.state < TcpState.timeWait.U
    val quoted = Reg(new IcmpErrorEvent)
    val epoch = Reg(UInt(32.W))
    val candidate = RegNext(io.error.valid && io.error.bits.protocol === 6.U && active &&
        io.error.bits.sourceIp === io.tuple.localIp &&
        io.error.bits.destinationIp === io.tuple.remoteIp &&
        io.error.bits.sourcePort === io.tuple.localPort &&
        io.error.bits.destinationPort === io.tuple.remotePort && !io.clear, false.B)
    when(io.error.valid) { quoted := io.error.bits; epoch := io.generation }
    val flight = (io.actual - io.oldest)(31, 0)
    val offset = (quoted.sequence - io.oldest)(31, 0)
    val live = candidate && epoch === io.generation && active && !io.clear &&
        flight =/= 0.U && !flight(31) && offset < flight &&
        quoted.sourceIp === io.tuple.localIp && quoted.destinationIp === io.tuple.remoteIp &&
        quoted.sourcePort === io.tuple.localPort && quoted.destinationPort === io.tuple.remotePort
    val tooBig = quoted.icmpType === 3.U && quoted.icmpCode === 4.U
    val mtu = PathMtuRules.decreased(quoted.nextHopMtu, quoted.totalLength, io.pathMtu)
    val validDecrease = live && tooBig && quoted.dontFragment && mtu >= 68.U &&
        mtu < io.pathMtu && mtu < quoted.totalLength && mtu <= io.limit
    val limit = RegInit(65535.U(16.W))
    val age = RegInit(0.U(log2Ceil(expiry + 1).W))
    when(io.clear) { age := 0.U; limit := Mux(io.limit < 68.U, 68.U, io.limit) }
        .elsewhen(io.update.valid && !io.update.bits.allowIncrease) { age := 0.U }
        .elsewhen(io.tick && active && io.pathMtu < limit && age < expiry.U) { age := age + 1.U }
    val expire = active && !io.clear && !validDecrease && age === expiry.U &&
        io.pathMtu < limit && io.actual === io.oldest && io.reserved === io.oldest
    val updateValid = RegNext(validDecrease || expire, false.B)
    val update = Reg(new TcpPathMtuUpdate)
    val updateSequence = Reg(UInt(32.W))
    when(validDecrease) { updateSequence := quoted.sequence }
    when(validDecrease || expire) {
        update.generation := io.generation
        update.mtu := Mux(validDecrease, mtu, limit)
        update.allowIncrease := !validDecrease
    }
    val stillIdle = io.actual === io.oldest && io.reserved === io.oldest
    when(io.update.valid && update.allowIncrease) { age := 0.U }
    io.update.valid := updateValid && update.generation === io.generation && active && !io.clear &&
        Mux(update.allowIncrease, stillIdle && !candidate && !validDecrease && update.mtu > io.pathMtu,
            update.mtu < io.pathMtu && ((updateSequence - io.oldest)(31, 0) < flight) && flight =/= 0.U && !flight(31))
    io.update.bits := update
    val noticeValid = RegNext(live && (!tooBig || validDecrease), false.B)
    val notice = Reg(new TcpNetworkError)
    when(live) {
        notice.generation := io.generation; notice.routerIp := quoted.routerIp
        notice.icmpType := quoted.icmpType; notice.icmpCode := quoted.icmpCode
        notice.pointer := quoted.pointer; notice.sequence := quoted.sequence
        notice.mtu := Mux(validDecrease, mtu, io.pathMtu)
    }
    io.notice.valid := noticeValid && notice.generation === io.generation && active && !io.clear &&
        ((notice.sequence - io.oldest)(31, 0) < flight) && flight =/= 0.U && !flight(31) &&
        (!(notice.icmpType === 3.U && notice.icmpCode === 4.U) ||
            (io.update.valid && !update.allowIncrease && update.mtu === notice.mtu))
    io.notice.bits := notice
}
