package netstack

import chisel3._
import chisel3.util._

class TcpTransmissionComplete extends Bundle {
    // Exclusive sequence end, including SYN/FIN's one sequence number.
    val endSequence = UInt(32.W)
    val retransmission = Bool()
    val syn = Bool()
}

/** One connection's RFC 6298 timer and conservative Karn RTT sampler.
  * tx is asserted only on actual transmission completion, never when queued.
  * ACKs still need the session engine's segment/state validation. This module
  * additionally rejects duplicate, old, future and half-space-ambiguous ACKs.
  * expired remains asserted until ACK progress or completed retransmission;
  * blocked output therefore cannot trigger repeated backoff without sending.
  * Defaults use a 1-ms tick. An extra tick on arming covers tick phase, so the
  * actual delay cannot be less than RTO. Sequence spans must remain < 2^31.
  */
class TcpRetransmissionTimer(val ticksPerSecond: Int = 1000, val maxSeconds: Int = 60) extends Module {
    require(ticksPerSecond >= 1 && ticksPerSecond <= 1000000)
    require(maxSeconds >= 60 && maxSeconds <= 600)
    val maxTicks = ticksPerSecond * maxSeconds
    val timerWidth = log2Ceil(maxTicks + 2)
    val fraction = 8
    val fixedWidth = timerWidth + fraction
    val io = IO(new Bundle {
        val clear = Input(Bool())
        val initialSequence = Input(UInt(32.W))
        val tick = Input(Bool())
        val tx = Flipped(Valid(new TcpTransmissionComplete))
        val ack = Flipped(Valid(UInt(32.W)))
        val handshakeComplete = Input(Bool())
        val expired = Output(Bool())
        val running = Output(Bool())
        val rto = Output(UInt(timerWidth.W))
        val remaining = Output(UInt(timerWidth.W))
        val oldest = Output(UInt(32.W))
        val next = Output(UInt(32.W))
        val ackProgress = Output(Bool())
        val invalidTx = Output(Bool())
        val rttKnown = Output(Bool())
        val sampleTaken = Output(Bool())
        val srttFixed = Output(UInt(fixedWidth.W))
        val varianceFixed = Output(UInt(fixedWidth.W))
    })
    def after(a: UInt, b: UInt): Bool = {
        val delta = (a - b)(31, 0)
        delta =/= 0.U && !delta(31)
    }
    def atLeast(a: UInt, b: UInt): Bool = a === b || after(a, b)
    val oldest = RegInit(0.U(32.W))
    val next = RegInit(0.U(32.W))
    val running = RegInit(false.B)
    val expired = RegInit(false.B)
    val rto = RegInit(ticksPerSecond.U(timerWidth.W))
    val remaining = RegInit(0.U(timerWidth.W))
    val known = RegInit(false.B)
    val srtt = RegInit(0.U(fixedWidth.W))
    val variance = RegInit(0.U(fixedWidth.W))
    val sampling = RegInit(false.B)
    val sampleEnd = Reg(UInt(32.W))
    val sampleAge = RegInit(0.U(timerWidth.W))
    val ambiguous = RegInit(false.B)
    val synOutstanding = RegInit(false.B)
    val synEnd = Reg(UInt(32.W))
    val synFallback = RegInit(false.B)

    val newSend = io.tx.valid && !io.tx.bits.retransmission &&
        after(io.tx.bits.endSequence, next) && after(io.tx.bits.endSequence, oldest)
    val retransmission = io.tx.valid && io.tx.bits.retransmission &&
        after(io.tx.bits.endSequence, oldest) && atLeast(next, io.tx.bits.endSequence)
    val nextSent = Mux(newSend, io.tx.bits.endSequence, next)
    val progress = io.ack.valid && after(io.ack.bits, oldest) && atLeast(nextSent, io.ack.bits)
    val nextOldest = Mux(progress, io.ack.bits, oldest)
    val outstanding = nextOldest =/= nextSent
    val sampleComplete = sampling && progress && atLeast(io.ack.bits, sampleEnd)
    val takeSample = sampleComplete && !ambiguous && !retransmission
    val ageWithTick = sampleAge +& io.tick.asUInt
    val measured = Mux(ageWithTick >= maxTicks.U, maxTicks.U, ageWithTick + 1.U)
    val sampleFixed = (measured << fraction).pad(fixedWidth)
    val delta = Mux(srtt >= sampleFixed, srtt - sampleFixed, sampleFixed - srtt)
    // Round upwards at fixed-point truncation boundaries, retaining 8 fraction bits.
    val updatedVariance = Mux(known, ((variance * 3.U +& delta +& 3.U) >> 2), sampleFixed >> 1)
    val updatedSrtt = Mux(known, ((srtt * 7.U +& sampleFixed +& 7.U) >> 3), sampleFixed)
    val varianceTerm = Mux((updatedVariance << 2) < (1 << fraction).U,
        (1 << fraction).U, updatedVariance << 2)
    val computedRto = (updatedSrtt +& varianceTerm +& ((1 << fraction) - 1).U) >> fraction
    val boundedRto = Mux(computedRto < ticksPerSecond.U, ticksPerSecond.U,
        Mux(computedRto > maxTicks.U, maxTicks.U, computedRto))
    val sampledRto = Mux(takeSample, boundedRto, rto)
    val effectiveRto = Mux(io.handshakeComplete && synFallback && sampledRto < (3 * ticksPerSecond).U,
        (3 * ticksPerSecond).U, sampledRto)
    val restart = outstanding && (progress || retransmission || (newSend && !running && !expired))
    val timeout = io.tick && running && remaining <= 1.U && !progress && !retransmission

    io.expired := expired
    io.running := running
    io.rto := rto
    io.remaining := remaining
    io.oldest := oldest
    io.next := next
    io.ackProgress := progress && !io.clear
    io.invalidTx := io.tx.valid && !newSend && !retransmission && !io.clear
    io.rttKnown := known
    io.sampleTaken := takeSample && !io.clear
    io.srttFixed := srtt
    io.varianceFixed := variance

    next := nextSent
    oldest := nextOldest
    rto := effectiveRto
    when(sampling && io.tick) { sampleAge := Mux(sampleAge < maxTicks.U, sampleAge + 1.U, sampleAge) }
    when(retransmission && sampling) { ambiguous := true.B }
    when(sampleComplete) { sampling := false.B }
    when(takeSample) {
        known := true.B
        srtt := updatedSrtt
        variance := updatedVariance
    }
    when(newSend && (!sampling || sampleComplete) && after(io.tx.bits.endSequence, nextOldest)) {
        sampling := true.B
        sampleEnd := io.tx.bits.endSequence
        sampleAge := 0.U
        ambiguous := false.B
    }
    when(newSend && io.tx.bits.syn) { synOutstanding := true.B; synEnd := io.tx.bits.endSequence }
    when(progress && ((synOutstanding && atLeast(io.ack.bits, synEnd)) ||
        (newSend && io.tx.bits.syn && atLeast(io.ack.bits, io.tx.bits.endSequence)))) {
        synOutstanding := false.B
    }
    when(io.handshakeComplete) { synFallback := false.B }

    when(!outstanding) {
        running := false.B
        expired := false.B
        remaining := 0.U
    }.elsewhen(restart) {
        running := true.B
        expired := false.B
        remaining := effectiveRto + 1.U
    }.elsewhen(timeout) {
        running := false.B
        expired := true.B
        remaining := 0.U
        rto := Mux((rto +& rto) > maxTicks.U, maxTicks.U, rto +& rto)
        when(synOutstanding && rto < (3 * ticksPerSecond).U) { synFallback := true.B }
    }.elsewhen(io.tick && running) { remaining := remaining - 1.U }

    when(io.clear) {
        oldest := io.initialSequence
        next := io.initialSequence
        running := false.B
        expired := false.B
        rto := ticksPerSecond.U
        remaining := 0.U
        known := false.B
        srtt := 0.U
        variance := 0.U
        sampling := false.B
        sampleAge := 0.U
        ambiguous := false.B
        synOutstanding := false.B
        synFallback := false.B
    }
}

class TcpTimerHarness extends Module {
    val io = IO(new Bundle {
        val clear = Input(Bool())
        val initialSequence = Input(UInt(32.W))
        val tick = Input(Bool())
        val txValid = Input(Bool())
        val endSequence = Input(UInt(32.W))
        val retransmission = Input(Bool())
        val syn = Input(Bool())
        val ackValid = Input(Bool())
        val ackSequence = Input(UInt(32.W))
        val handshakeComplete = Input(Bool())
        val expired = Output(Bool())
        val running = Output(Bool())
        val rto = Output(UInt(16.W))
        val remaining = Output(UInt(16.W))
        val oldest = Output(UInt(32.W))
        val next = Output(UInt(32.W))
        val ackProgress = Output(Bool())
        val invalidTx = Output(Bool())
        val rttKnown = Output(Bool())
        val sampleTaken = Output(Bool())
        val srttFixed = Output(UInt(32.W))
        val varianceFixed = Output(UInt(32.W))
    })
    // One test tick represents 100 ms, keeping the same 1/3/60-second limits.
    val dut = Module(new TcpRetransmissionTimer(10))
    dut.io.clear := io.clear
    dut.io.initialSequence := io.initialSequence
    dut.io.tick := io.tick
    dut.io.tx.valid := io.txValid
    dut.io.tx.bits.endSequence := io.endSequence
    dut.io.tx.bits.retransmission := io.retransmission
    dut.io.tx.bits.syn := io.syn
    dut.io.ack.valid := io.ackValid
    dut.io.ack.bits := io.ackSequence
    dut.io.handshakeComplete := io.handshakeComplete
    io.expired := dut.io.expired
    io.running := dut.io.running
    io.rto := dut.io.rto
    io.remaining := dut.io.remaining
    io.oldest := dut.io.oldest
    io.next := dut.io.next
    io.ackProgress := dut.io.ackProgress
    io.invalidTx := dut.io.invalidTx
    io.rttKnown := dut.io.rttKnown
    io.sampleTaken := dut.io.sampleTaken
    io.srttFixed := dut.io.srttFixed
    io.varianceFixed := dut.io.varianceFixed
}
