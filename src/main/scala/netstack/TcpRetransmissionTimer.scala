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
class TcpRetransmissionTimer(val ticksPerSecond: Int = 1000, val maxSeconds: Int = 60,
    val pipelineRto: Boolean = true) extends Module {
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
    // Compare both candidate right edges before the late TX-completion
    // qualification. Selecting sequence words before subtract/compare makes
    // ledger/tag validation part of the timer arithmetic critical path.
    val ackWithin = Mux(newSend, atLeast(io.tx.bits.endSequence, io.ack.bits),
        atLeast(next, io.ack.bits))
    val progress = io.ack.valid && after(io.ack.bits, oldest) && ackWithin
    val nextOldest = Mux(progress, io.ack.bits, oldest)
    val outstanding = Mux(newSend,
        Mux(progress, io.ack.bits =/= io.tx.bits.endSequence, oldest =/= io.tx.bits.endSequence),
        Mux(progress, io.ack.bits =/= next, oldest =/= next))
    val sampleComplete = sampling && progress && atLeast(io.ack.bits, sampleEnd)
    val takeSample = sampleComplete && !ambiguous && !retransmission
    // Tick is a late clock-enable input. Compute both bounded ages in
    // parallel so it does not feed a carry chain before RTT arithmetic.
    val measuredNoTick = Mux(sampleAge >= (maxTicks - 1).U, maxTicks.U, sampleAge +& 1.U)
    val measuredTick = Mux(sampleAge >= (maxTicks - 2).U, maxTicks.U, sampleAge +& 2.U)
    val measured = Mux(io.tick, measuredTick, measuredNoTick)
    // Reduce multiple operands with carry-save compressors; only the final
    // pair propagates a carry. Bounds include every operand, including shifts.
    def sumMany(xs: Seq[UInt]): UInt = {
        val width = xs.map(_.getWidth).max + log2Ceil(xs.size)
        def compress(ys: Seq[UInt]): UInt = {
            if (ys.size == 1) ys.head
            else if (ys.size == 2) ys.head +& ys(1)
            else compress(ys.grouped(3).flatMap {
                case Seq(a, b, c) => Seq((a ^ b ^ c)(width - 1, 0),
                    (((a & b) | (b & c) | (a & c)) << 1)(width - 1, 0))
                case rest => rest
            }.toSeq)
        }
        compress(xs.map(_.pad(width)))
    }
    val sampleFixed = (measured << fraction).pad(fixedWidth)
    val delta = Mux(srtt >= sampleFixed, srtt - sampleFixed, sampleFixed - srtt)
    // Round upwards at fixed-point truncation boundaries, retaining 8 fraction bits.
    val updatedVariance = Mux(known, (sumMany(Seq(variance, variance << 1, delta, 3.U)) >> 2), sampleFixed >> 1)
    val updatedSrtt = Mux(known, (sumMany(Seq(srtt, srtt << 1, srtt << 2, sampleFixed, 7.U)) >> 3), sampleFixed)
    // SRTT/RTTVAR commit on the ACK edge; RTO bounding/rearm follows on
    // the next clock. The timer waits during that single cycle, so this
    // boundary can only postpone, never advance, the retransmit deadline.
    val estimateReady = RegNext(takeSample && !io.clear, false.B)
    val rtoSrtt = if (pipelineRto) srtt else updatedSrtt
    val rtoVariance = if (pipelineRto) variance else updatedVariance
    val varianceTerm = Mux((rtoVariance << 2) < (1 << fraction).U,
        (1 << fraction).U, rtoVariance << 2)
    val computedRto = sumMany(Seq(rtoSrtt, varianceTerm, ((1 << fraction) - 1).U)) >> fraction
    val boundedRto = Mux(computedRto < ticksPerSecond.U, ticksPerSecond.U,
        Mux(computedRto > maxTicks.U, maxTicks.U, computedRto))
    val sampledRto = Mux(if (pipelineRto) estimateReady else takeSample, boundedRto, rto)
    val effectiveRto = Mux(io.handshakeComplete && synFallback && sampledRto < (3 * ticksPerSecond).U,
        (3 * ticksPerSecond).U, sampledRto)
    val restart = outstanding && (progress || retransmission || (newSend && !running && !expired))
    val estimating = if (pipelineRto) takeSample else false.B
    val estimateComplete = if (pipelineRto) estimateReady else false.B
    val timeout = io.tick && running && remaining <= 1.U && !progress && !retransmission &&
        !estimating && !estimateComplete

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
    val sampleStillOutstanding = Mux(progress, after(io.tx.bits.endSequence, io.ack.bits),
        after(io.tx.bits.endSequence, oldest))
    when(newSend && (!sampling || sampleComplete) && sampleStillOutstanding) {
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
    }.elsewhen(estimating) {
        running := false.B
        expired := false.B
        remaining := 0.U
    }.elsewhen(restart || estimateComplete) {
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

class TcpTimerHarness(val pipelined: Boolean = false) extends Module {
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
    val dut = Module(new TcpRetransmissionTimer(10, pipelineRto = pipelined))
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

class TcpPipelinedTimerHarness extends TcpTimerHarness(true)
