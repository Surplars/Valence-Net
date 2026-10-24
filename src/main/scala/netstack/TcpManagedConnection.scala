package netstack

import chisel3._
import chisel3.util._

/** Connect a TCB to its autonomous retransmission timer. The external data
  * scheduler handles replay requests and reports physical transmission
  * completions; queuing a command does not count as an attempted transmission.
  * A bounded number of completed retransmissions is allowed before aborting.
  */
class TcpManagedConnection(val ticksPerSecond: Int = 1000, val timeWaitTicks: Int = 120000,
                           val challengeTicks: Int = 10, val maxRetries: Int = 8,
                           val trustedReservations: Boolean = false, val receiveReorder: Boolean = false, val sackSupport: Boolean = false) extends Module {
    require(maxRetries >= 1 && maxRetries <= 255)
    val io = IO(new TcpConnectionIO {
        val timeout = Valid(UInt(32.W))
        val retransmissionTimeout = Output(UInt(32.W))
        val timerRunning = Output(Bool())
        val timerExpired = Output(Bool())
        val retries = Output(UInt(8.W))
    })
    val control = Module(new TcpConnectionControl(timeWaitTicks, challengeTicks, trustedReservations, receiveReorder, sackSupport))
    val timer = Module(new TcpRetransmissionTimer(ticksPerSecond))
    control.io.open <> io.open
    control.io.passiveInitialSequence := io.passiveInitialSequence
    control.io.pathMtuLimit := io.pathMtuLimit
    control.io.pathMtuUpdate <> io.pathMtuUpdate
    io.pathMtu := control.io.pathMtu
    io.pathMtuChanged := control.io.pathMtuChanged
    control.io.rx <> io.rx
    io.disposition <> control.io.disposition
    io.control <> control.io.control
    io.controlPending := control.io.controlPending
    io.ack <> control.io.ack
    io.notice <> control.io.notice
    control.io.sendReserve <> io.sendReserve
    io.sendSequence := control.io.sendSequence
    control.io.completed <> io.completed
    io.completionAccepted := control.io.completionAccepted
    io.invalidCompletion := control.io.invalidCompletion
    io.replay <> control.io.replay
    control.io.probe <> io.probe
    control.io.receiveWindow := io.receiveWindow
    control.io.receiveSack := io.receiveSack; io.sackEnabled := control.io.sackEnabled
    control.io.congestionWindow := io.congestionWindow
    control.io.tick := io.tick
    control.io.updateWindow := io.updateWindow
    io.state := control.io.state
    io.generation := control.io.generation
    io.sendOldest := control.io.sendOldest
    io.sendNext := control.io.sendNext
    io.sendActual := control.io.sendActual
    io.receiveCredit := control.io.receiveCredit
    io.receiveNext := control.io.receiveNext
    io.sendWindow := control.io.sendWindow
    io.sendCredit := control.io.sendCredit
    io.dataHeader := control.io.dataHeader
    io.sendMss := control.io.sendMss
    io.peerScale := control.io.peerScale
    io.scaleEnabled := control.io.scaleEnabled

    val inactive = control.io.state === TcpState.closed.U || control.io.state === TcpState.listen.U ||
        control.io.state === TcpState.timeWait.U
    val resetTimer = inactive || control.io.open.fire
    timer.io.clear := resetTimer
    timer.io.initialSequence := Mux(control.io.open.fire, io.open.bits.initialSequence,
        Mux(control.io.state === TcpState.listen.U && control.io.rx.fire,
            io.passiveInitialSequence, control.io.sendOldest))
    timer.io.tick := io.tick
    timer.io.tx.valid := control.io.completionAccepted
    timer.io.tx.bits.endSequence := io.completed.bits.endSequence
    timer.io.tx.bits.retransmission := io.completed.bits.retransmission
    timer.io.tx.bits.syn := control.io.state === TcpState.synSent.U || control.io.state === TcpState.synReceived.U

    // Observe an owned ACK event once when it becomes available. Waiting for
    // a stalled external reclamation consumer would falsely delay its RTT.
    val ackSeen = RegInit(false.B)
    val freshAck = control.io.ack.valid && !ackSeen
    when(!control.io.ack.valid) { ackSeen := false.B }
    when(freshAck) { ackSeen := true.B }
    timer.io.ack.valid := freshAck
    timer.io.ack.bits := control.io.ack.bits.acknowledgement
    timer.io.handshakeComplete := freshAck && control.io.ack.bits.handshake
    val progress = freshAck && control.io.ack.bits.acknowledgement =/= control.io.ack.bits.previous

    val retryQueued = RegInit(false.B)
    val retries = RegInit(0.U(8.W))
    val timeoutAbort = timer.io.expired && retries >= maxRetries.U && !inactive
    control.io.close.valid := io.close.valid || timeoutAbort
    control.io.close.bits := Mux(timeoutAbort, true.B, io.close.bits)
    io.close.ready := control.io.close.ready && !timeoutAbort
    io.timeout.valid := timeoutAbort && control.io.close.fire
    io.timeout.bits := control.io.generation

    control.io.retry.valid := timer.io.expired && !retryQueued && !timeoutAbort && !inactive
    control.io.retry.bits := control.io.generation
    when(control.io.retry.fire) { retryQueued := true.B }
    when(control.io.completionAccepted && io.completed.bits.timeoutRetry) {
        retryQueued := false.B
        // The completion carries its origin through every queue. An older
        // PMTU/fast replay completing while an RTO retry is queued must not
        // release that retry's ownership or consume its loss budget.
        when(retries < 255.U) { retries := retries + 1.U }
    }
    when(progress || resetTimer) { retryQueued := false.B; retries := 0.U }
    io.retransmissionTimeout := timer.io.rto
    io.timerRunning := timer.io.running
    io.timerExpired := timer.io.expired
    io.retries := retries
}
