package iperf

import chisel3._
import chisel3.util._
import soc.ip.bus._

/** Read-only RX counters, sampled atomically every millisecond. All fourteen
  * wrapping UInt32 words are low-first: accepted, dropped, badFcs, reason[0..7],
  * stalledCycles, longestStall, inbandLinkDrops. Reset starts a new epoch.
  * Neither the mailbox nor UART can backpressure the packet path.
  */
class RxDiagnosticSnapshot(sampleCycles: Int = 125000) extends RawModule {
    require(sampleCycles >= 2)
    val sourceClock = IO(Input(Clock()))
    val destinationClock = IO(Input(Clock()))
    val commonReset = IO(Input(AsyncReset()))
    val accepted = IO(Input(Bool()))
    val dropped = IO(Input(Bool()))
    val badFcs = IO(Input(Bool()))
    val dropReasons = IO(Input(UInt(8.W)))
    val backpressured = IO(Input(Bool()))
    val inbandLink = IO(Input(Bool()))
    val totals = IO(Output(UInt(448.W)))
    val sourceRelease = Module(new CdcResetRelease)
    sourceRelease.clockIn := sourceClock
    sourceRelease.asyncReset := commonReset
    val destinationRelease = Module(new CdcResetRelease)
    destinationRelease.clockIn := destinationClock
    destinationRelease.asyncReset := commonReset
    // Same held-payload/ACK primitive as upstream. Payload and toggle paths
    // require scoped physical CDC constraints in addition to ASYNC_REG.
    val mailbox = Module(new CdcMailbox(448))
    mailbox.sourceClock := sourceClock
    mailbox.destinationClock := destinationClock
    mailbox.commonReset := commonReset
    withClockAndReset(sourceClock, sourceRelease.resetOut) {
        val counts = RegInit(VecInit(Seq.fill(14)(0.U(32.W))))
        when(accepted) { counts(0) := counts(0) + 1.U }
        when(dropped) { counts(1) := counts(1) + 1.U }
        when(badFcs) { counts(2) := counts(2) + 1.U }
        for (reason <- 0 until 8) {
            when(dropReasons(reason)) { counts(3 + reason) := counts(3 + reason) + 1.U }
        }
        // Count frame-output valid&&!ready cycles, not wire idle time.
        val consecutiveStall = RegInit(0.U(32.W))
        val nextStall = consecutiveStall + 1.U
        when(backpressured) {
            counts(11) := counts(11) + 1.U
            // Saturate an individual interval; cumulative counters wrap.
            when(!consecutiveStall.andR) { consecutiveStall := nextStall }
            when(nextStall > counts(12)) { counts(12) := nextStall }
        }.otherwise { consecutiveStall := 0.U }
        val sawLink = RegInit(false.B)
        val previousLink = RegInit(false.B)
        when(inbandLink) { sawLink := true.B }
        when(sawLink && previousLink && !inbandLink) { counts(13) := counts(13) + 1.U }
        previousLink := inbandLink
        val sampleTimer = RegInit(0.U(log2Ceil(sampleCycles).W))
        val pending = RegInit(false.B)
        val due = sampleTimer === (sampleCycles - 1).U
        sampleTimer := Mux(due, 0.U, sampleTimer + 1.U)
        when(due) { pending := true.B }
        mailbox.source.valid := pending
        mailbox.source.bits := counts.asUInt
        when(mailbox.source.fire) { pending := due }
    }
    withClockAndReset(destinationClock, destinationRelease.resetOut) {
        val latest = RegInit(0.U(448.W))
        mailbox.destination.ready := true.B
        when(mailbox.destination.fire) { latest := mailbox.destination.bits }
        totals := latest
    }
}
