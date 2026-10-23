package netstack

import chisel3._
import chisel3.util._

/** A MAC/routing adapter must return one ordered completion per IP packet.
  * Echo BOTH tag and generation, including for packets dropped before the wire.
  * Completion is legal starting the cycle AFTER the final IP input handshake.
  * failed=true aborts the connection rather than acknowledging an unsent range.
  */
class TcpWireResult extends Bundle {
    val tag = UInt(32.W)
    val generation = UInt(32.W)
    val failed = Bool()
}
class TcpWireDescriptor extends Bundle {
    val probe = Bool()
    val tag = UInt(32.W)
    val completion = new TcpConnectionCompletion
    val sequenceSpace = Bool()
    val destinationIp = UInt(32.W)
    val context = UInt(64.W)
    val data = Bool()
}

/** One connection's complete TX path, including control/data arbitration,
  * ordered physical-completion ownership, retained payload, and automatic RTO.
  * Receive input is already validated and durably stored by the RX allocator.
  * Abort cancels unoffered data and incomplete application writes. An IP frame
  * already offered (even with ready low) remains stable until its last beat.
  * Reopening waits for old frames/completions to drain; generation alone never
  * substitutes for draining. Global reset must also reset the MAC adapter.
  */
class TcpConnectionTransmit(val banks: Int = 8, val beatsPerBank: Int = 128,
    val ticksPerSecond: Int = 1000, val timeWaitTicks: Int = 120000,
    val challengeTicks: Int = 10, val maxRetries: Int = 8,
    val completionDepth: Int = 16, val autonomousCongestion: Boolean = false,
    val receiveReorder: Boolean = false, val sackSupport: Boolean = false,
    val pipelineCompletions: Boolean = false) extends Module {
    require(completionDepth >= 2)
    val io = IO(new Bundle {
        val open = Flipped(Decoupled(new TcpConnectionOpen))
        val passiveInitialSequence = Input(UInt(32.W))
        val close = Flipped(Decoupled(Bool()))
        val rx = Flipped(Decoupled(new TcpConnectionRx))
        val disposition = Decoupled(new TcpRxDisposition)
        val rxConsumed = Output(Bool())
        val ownershipDrained = Output(Bool())
        val ack = Decoupled(new TcpAckUpdate)
        val notice = Decoupled(new TcpConnectionNotice)
        val receiveWindow = Input(UInt(32.W))
        val receiveSack = Input(new TcpSackBlocks)
        val congestionWindow = Input(UInt(32.W)) // upper cap when autonomous policy is enabled
        val congestionLimit = Output(UInt(32.W)); val congestionThreshold = Output(UInt(32.W))
        val fastRecovery = Output(Bool())
        val tick = Input(Bool())
        val updateWindow = Input(Bool())
        val allocate = Flipped(Decoupled(new TcpStoreAllocation))
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val stored = Decoupled(new TcpStoreResult)
        val out = Decoupled(new PacketBeat(16))
        val packet = Output(new TcpWireDescriptor)
        val completed = Flipped(Valid(new TcpWireResult))
        val invalidCompletion = Output(Bool())
        val completionAccepted = Output(Bool())
        val fault = Output(Bool())
        val timeout = Valid(UInt(32.W))
        val state = Output(UInt(4.W)); val generation = Output(UInt(32.W))
        val sendOldest = Output(UInt(32.W)); val sendNext = Output(UInt(32.W))
        val receiveCredit = Output(UInt(32.W))
        val sendActual = Output(UInt(32.W)); val receiveNext = Output(UInt(32.W))
        val sendMss = Output(UInt(16.W))
        val freeBanks = Output(UInt(log2Ceil(banks + 1).W))
        val outstanding = Output(UInt(log2Ceil(completionDepth + 1).W))
        val timerRunning = Output(Bool()); val timerExpired = Output(Bool())
        val rto = Output(UInt(32.W)); val retries = Output(UInt(8.W))
    })
    val connection = Module(new TcpManagedConnection(ticksPerSecond, timeWaitTicks, challengeTicks, maxRetries, trustedReservations = true, receiveReorder = receiveReorder, sackSupport = sackSupport))
    val inactive = connection.io.state === TcpState.closed.U || connection.io.state === TcpState.listen.U
    val receiveEvents = Module(new Queue(new TcpConnectionRx, 1))
    val offered = RegInit(false.B)
    val offeredData = RegInit(false.B)
    val data = withReset(reset.asBool || (inactive && !offeredData)) {
        Module(new TcpDataTransmit(banks, beatsPerBank))
    }
    val control = Module(new Ipv4TcpTransmit(2))
    val controlDescriptors = Module(new Queue(new TcpControlPacket, 4))
    val ledger = Module(new Queue(new TcpWireDescriptor, completionDepth))
    val arbiter = Module(new PacketArbiter)
    val nextTag = RegInit(0.U(32.W))
    val failed = RegInit(false.B)
    val emptyPayload = RegInit(false.B)

    // A validated result remains owned while its registered TCB update commits.
    val completionPending = if (pipelineCompletions) RegNext(ledger.io.deq.fire, false.B) else false.B
    // Preserve same-cycle ordering for sequence-consuming completions, without
    // putting local tag/generation validation back on the RX-control path.
    val completionBarrier = if (pipelineCompletions)
        io.completed.valid && ledger.io.deq.bits.sequenceSpace else false.B
    val reusable = !completionPending && !offered && !ledger.io.deq.valid && !controlDescriptors.io.deq.valid &&
        !connection.io.controlPending && !emptyPayload
    io.ownershipDrained := reusable && !receiveEvents.io.deq.valid && !failed
    connection.io.open.valid := io.open.valid && reusable && !receiveEvents.io.deq.valid && !failed
    connection.io.open.bits := io.open.bits
    io.open.ready := connection.io.open.ready && reusable && !receiveEvents.io.deq.valid && !failed
    connection.io.passiveInitialSequence := io.passiveInitialSequence
    val receiveAllowed = connection.io.state =/= TcpState.listen.U || reusable
    // Register committed metadata before the arithmetic/control path.
    // The RX allocator retains its payload while this event awaits disposition.
    val enqueueReceive = receiveAllowed && !failed && !connection.io.open.fire
    receiveEvents.io.enq.valid := io.rx.valid && enqueueReceive
    receiveEvents.io.enq.bits := io.rx.bits
    io.rx.ready := receiveEvents.io.enq.ready && enqueueReceive
    connection.io.rx.valid := receiveEvents.io.deq.valid && receiveAllowed && !failed && !completionBarrier
    connection.io.rx.bits := receiveEvents.io.deq.bits
    receiveEvents.io.deq.ready := connection.io.rx.ready && receiveAllowed && !failed && !completionBarrier
    io.disposition <> connection.io.disposition
    io.rxConsumed := connection.io.rx.fire && !connection.io.rx.bits.reassembled
    io.ack <> connection.io.ack
    io.notice <> connection.io.notice
    connection.io.receiveWindow := io.receiveWindow
    connection.io.receiveSack := io.receiveSack
    connection.io.congestionWindow := io.congestionWindow
    connection.io.tick := io.tick
    connection.io.updateWindow := io.updateWindow
    val closeAllowed = io.close.bits || !data.io.unsent
    connection.io.close.valid := failed || (io.close.valid && closeAllowed)
    connection.io.close.bits := failed || io.close.bits
    io.close.ready := connection.io.close.ready && closeAllowed && !failed
    when(connection.io.close.fire && failed) { failed := false.B }

    val canSend = connection.io.state === TcpState.established.U || connection.io.state === TcpState.closeWait.U
    val allocateAllowed = canSend && !io.close.valid && !failed
    data.io.allocate.valid := io.allocate.valid && allocateAllowed
    data.io.allocate.bits := io.allocate.bits
    io.allocate.ready := data.io.allocate.ready && allocateAllowed
    data.io.in <> io.in
    io.stored <> data.io.stored
    data.io.enabled := canSend && !failed && !(io.close.valid && io.close.bits)
    data.io.generation := connection.io.generation
    data.io.sendOldest := connection.io.sendOldest
    data.io.sendActual := connection.io.sendActual
    data.io.sendSequence := connection.io.sendSequence
    data.io.credit := connection.io.sendCredit
    // A peer's advertised MSS can exceed the local retained bank size.
    // Data options are either absent or one to four padded SACK blocks.
    // Compute each payload budget in parallel with the live ACK/generation
    // compatibility check. Do not subtract/clip after selecting option length:
    // that serializes three carry chains before payload RAM validation.
    val optionLengths = Seq(0, 12, 20, 28, 36)
    val payloadBudgets = optionLengths.map { bytes =>
        val reduced = Mux(connection.io.sendMss > bytes.U,
            connection.io.sendMss - bytes.U, 1.U)
        Mux(reduced > (beatsPerBank * 16).U, (beatsPerBank * 16).U, reduced)
    }
    data.io.mss := Mux1H(optionLengths.zip(payloadBudgets).map { case (bytes, budget) =>
        (connection.io.dataHeader.optionLength === bytes.U) -> budget
    })
    data.io.header := connection.io.dataHeader
    connection.io.sendReserve <> data.io.reserve
    data.io.replay <> connection.io.replay
    data.io.replayLimit := 65535.U
    io.congestionLimit := io.congestionWindow
    io.congestionThreshold := 0.U; io.fastRecovery := false.B
    if (autonomousCongestion) {
        val cc = Module(new TcpCongestionControl)
        cc.io.clear := connection.io.state < TcpState.established.U || connection.io.state === TcpState.timeWait.U
        // SMSS is the largest payload this connection can send. A particular
        // packet may have a smaller budget because of its SACK options, but
        // receive-gap publication must not drive Reno's MSS/arithmetic path.
        cc.io.mss := Mux(connection.io.sendMss > (beatsPerBank * 16).U,
            (beatsPerBank * 16).U, connection.io.sendMss)
        cc.io.oldest := connection.io.sendOldest
        cc.io.flight := connection.io.sendActual - connection.io.sendOldest
        cc.io.timeout := connection.io.timerExpired
        val seenAck = RegInit(false.B)
        when(!connection.io.ack.valid) { seenAck := false.B }
        when(connection.io.ack.valid) { seenAck := true.B }
        cc.io.ack.valid := connection.io.ack.valid && !seenAck
        cc.io.ack.bits := connection.io.ack.bits
        val idleTicks = RegInit(0.U(32.W))
        val lastActual = RegNext(connection.io.sendActual)
        val idleLimit = connection.io.retransmissionTimeout
        cc.io.idleRestart := io.tick && cc.io.flight === 0.U && idleTicks === idleLimit
        when(cc.io.clear || cc.io.flight =/= 0.U || lastActual =/= connection.io.sendActual) { idleTicks := 0.U }
            .elsewhen(io.tick && idleTicks <= idleLimit) { idleTicks := idleTicks + 1.U }
        connection.io.congestionWindow := Mux(cc.io.window < io.congestionWindow, cc.io.window, io.congestionWindow)
        data.io.replay.valid := connection.io.replay.valid || cc.io.replay.valid
        data.io.replay.bits := Mux(connection.io.replay.valid, connection.io.replay.bits, cc.io.replay.bits)
        connection.io.replay.ready := data.io.replay.ready
        cc.io.replay.ready := data.io.replay.ready && !connection.io.replay.valid
        if (sackSupport) {
            val sack = Module(new TcpSackRecovery)
            sack.io.enabled := connection.io.sackEnabled
            // OPEN can fire only in CLOSED, already included in inactive.
            // Keep that invariant out of the replay-valid combinational cone.
            sack.io.clear := inactive || connection.io.state === TcpState.timeWait.U
            sack.io.timeout := connection.io.timerExpired
            sack.io.generation := connection.io.generation
            sack.io.oldest := connection.io.sendOldest; sack.io.actual := connection.io.sendActual
            sack.io.ack.valid := cc.io.ack.valid; sack.io.ack.bits := cc.io.ack.bits
            sack.io.request.valid := cc.io.replay.valid && connection.io.sackEnabled
            sack.io.request.bits := cc.io.replay.bits
            sack.io.prepared := data.io.prepared
            sack.io.miss := data.io.replayMiss
            val fastValid = Mux(connection.io.sackEnabled, sack.io.replay.valid, cc.io.replay.valid)
            val fastSequence = Mux(connection.io.sackEnabled, sack.io.replay.bits, cc.io.replay.bits)
            data.io.replay.valid := connection.io.replay.valid || fastValid
            data.io.replay.bits := Mux(connection.io.replay.valid, connection.io.replay.bits, fastSequence)
            data.io.replayLimit := Mux(connection.io.replay.valid || !connection.io.sackEnabled, 65535.U, sack.io.limit)
            sack.io.replay.ready := data.io.replay.ready && !connection.io.replay.valid
            cc.io.replay.ready := Mux(connection.io.sackEnabled, sack.io.request.ready,
                data.io.replay.ready && !connection.io.replay.valid)
        }
        io.congestionLimit := connection.io.congestionWindow
        io.congestionThreshold := cc.io.threshold; io.fastRecovery := cc.io.recovering
    }
    io.freeBanks := data.io.freeBanks

    val commandRoom = controlDescriptors.io.enq.ready && !emptyPayload
    control.io.command.valid := connection.io.control.valid && commandRoom
    control.io.command.bits := connection.io.control.bits.command
    connection.io.control.ready := control.io.command.ready && commandRoom
    controlDescriptors.io.enq.valid := connection.io.control.valid && control.io.command.ready && !emptyPayload
    controlDescriptors.io.enq.bits := connection.io.control.bits
    when(control.io.command.fire) { emptyPayload := true.B }
    control.io.in.valid := emptyPayload
    control.io.in.bits := 0.U.asTypeOf(new PacketBeat(16))
    control.io.in.bits.last := true.B
    when(control.io.in.fire) { emptyPayload := false.B }

    // Reserve a ledger slot before offering a packet, not after its first beat.
    // Only this stream can enqueue, so that slot cannot disappear mid-packet.
    val slot = offered || ledger.io.enq.ready
    val exposeData = !inactive || offeredData
    arbiter.io.in(0).valid := control.io.out.valid && slot
    arbiter.io.in(0).bits := control.io.out.bits
    control.io.out.ready := arbiter.io.in(0).ready && slot
    arbiter.io.in(1).valid := data.io.out.valid && slot && exposeData
    arbiter.io.in(1).bits := data.io.out.bits
    data.io.out.ready := arbiter.io.in(1).ready && slot && exposeData
    io.out <> arbiter.io.out
    when(io.out.valid && !offered) { offered := true.B; offeredData := arbiter.io.selected }
    when(io.out.fire && io.out.bits.last) { offered := false.B; offeredData := false.B; nextTag := nextTag + 1.U }
    controlDescriptors.io.deq.ready := control.io.out.fire && control.io.out.bits.last

    val meta = Wire(new TcpWireDescriptor)
    meta := 0.U.asTypeOf(new TcpWireDescriptor)
    meta.tag := nextTag
    meta.data := arbiter.io.selected
    val cp = controlDescriptors.io.deq.bits
    meta.completion.generation := Mux(meta.data, data.io.packet.generation, cp.generation)
    meta.completion.endSequence := Mux(meta.data, data.io.packet.sequence + data.io.packet.length,
        cp.command.sequence + cp.command.flags(1).asUInt + cp.command.flags(0).asUInt)
    meta.completion.retransmission := Mux(meta.data, data.io.packet.retransmission, cp.retransmission)
    meta.sequenceSpace := meta.data || cp.sequenceSpace
    meta.probe := !meta.data && cp.probe
    meta.destinationIp := Mux(meta.data, connection.io.dataHeader.destinationIp, cp.command.destinationIp)
    meta.context := Mux(meta.data, data.io.packet.context, 0.U)
    io.packet := meta
    ledger.io.enq.valid := io.out.fire && io.out.bits.last
    ledger.io.enq.bits := meta

    val done = ledger.io.deq.bits
    val matchResult = ledger.io.deq.valid && io.completed.bits.tag === done.tag &&
        io.completed.bits.generation === done.completion.generation
    ledger.io.deq.ready := io.completed.valid && matchResult
    io.invalidCompletion := io.completed.valid && !matchResult
    io.completionAccepted := io.completed.valid && matchResult
    val completedSequence = ledger.io.deq.fire && !io.completed.bits.failed && done.sequenceSpace
    if (pipelineCompletions) {
        connection.io.completed.valid := RegNext(completedSequence, false.B)
        connection.io.completed.bits := RegEnable(done.completion, ledger.io.deq.fire)
    } else {
        connection.io.completed.valid := completedSequence
        connection.io.completed.bits := done.completion
    }
    when(ledger.io.deq.fire && io.completed.bits.failed &&
        done.completion.generation === connection.io.generation && !inactive) { failed := true.B }
    // Zero-window persist is independent of the loss/RTO retry budget.
    // A queued probe cannot trigger repeated attempts while the MAC is blocked.
    // Keep its outstanding ownership even if the window reopens before its
    // completion, so a late callback cannot arm a later probe accidentally.
    val persistWidth = log2Ceil(ticksPerSecond * 60 + 2)
    val persistPeriod = RegInit(ticksPerSecond.U(persistWidth.W))
    val persistRemaining = RegInit(0.U(persistWidth.W))
    val persistActive = RegInit(false.B)
    val persistRequest = RegInit(false.B)
    val persistQueued = RegInit(false.B)
    val needPersist = canSend && data.io.unsent && connection.io.sendWindow === 0.U &&
        connection.io.sendActual === connection.io.sendOldest
    val initialPersist = connection.io.retransmissionTimeout
    connection.io.probe.valid := persistRequest && needPersist && !persistQueued
    connection.io.probe.bits := true.B
    when(!needPersist) {
        persistActive := false.B; persistRequest := false.B; persistRemaining := 0.U
        persistPeriod := initialPersist
    }.elsewhen(!persistActive && !persistQueued) {
        persistActive := true.B; persistRemaining := initialPersist + 1.U; persistPeriod := initialPersist
    }.elsewhen(io.tick && persistActive && !persistRequest && !persistQueued) {
        when(persistRemaining <= 1.U) { persistRequest := true.B }
            .otherwise { persistRemaining := persistRemaining - 1.U }
    }
    when(connection.io.probe.fire) {
        persistQueued := true.B; persistRequest := false.B; persistActive := false.B
    }
    when(ledger.io.deq.fire && done.probe && persistQueued) {
        persistQueued := false.B
        when(needPersist) {
            val doubled = persistPeriod +& persistPeriod
            val interval = Mux(doubled > (ticksPerSecond * 60).U, (ticksPerSecond * 60).U, doubled)
            persistActive := true.B; persistPeriod := interval; persistRemaining := interval + 1.U
        }
    }
    io.outstanding := ledger.io.count
    // The scheduler derives this request from the same cycle's credit/MSS.
    // Its invariant is monitored in parallel instead of serially rechecking it
    // in TCB ready -> RAM-read acceptance -> descriptor register enable.
    val invalidReservation = connection.io.sendReserve.fire &&
        (connection.io.sendReserve.bits === 0.U || connection.io.sendReserve.bits > connection.io.sendMss ||
            connection.io.sendReserve.bits > connection.io.sendCredit)
    io.fault := RegNext(invalidReservation, false.B) || data.io.fault || control.io.dropped ||
        (control.io.out.valid && !controlDescriptors.io.deq.valid) ||
        (ledger.io.enq.valid && !ledger.io.enq.ready) || connection.io.invalidCompletion
    io.timeout <> connection.io.timeout
    io.state := connection.io.state; io.generation := connection.io.generation
    io.sendOldest := connection.io.sendOldest; io.sendNext := connection.io.sendNext
    io.sendActual := connection.io.sendActual; io.receiveCredit := connection.io.receiveCredit; io.receiveNext := connection.io.receiveNext
    io.sendMss := connection.io.sendMss
    io.timerRunning := connection.io.timerRunning; io.timerExpired := connection.io.timerExpired
    io.rto := connection.io.retransmissionTimeout; io.retries := connection.io.retries
}
