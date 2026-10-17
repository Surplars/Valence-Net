package netstack

import chisel3._
import chisel3.util._

object TcpState {
    val closed = 0; val listen = 1; val synSent = 2; val synReceived = 3
    val established = 4; val finWait1 = 5; val finWait2 = 6; val closeWait = 7
    val closing = 8; val lastAck = 9; val timeWait = 10
}
class TcpConnectionOpen extends Bundle {
    val passive = Bool()
    val localIp = UInt(32.W); val remoteIp = UInt(32.W)
    val localPort = UInt(16.W); val remotePort = UInt(16.W)
    val initialSequence = UInt(32.W)
    val localMss = UInt(16.W)
    val offerScale = Bool(); val localScale = UInt(4.W)
}
class TcpConnectionRx extends Bundle {
    val segment = new TcpSegment
    val payloadToken = UInt(32.W)
    // The complete payload must already be retained in TcpPayloadStore.
    val payloadStored = Bool()
}
class TcpRxDisposition extends Bundle {
    val generation = UInt(32.W); val token = UInt(32.W)
    val keep = Bool(); val skip = UInt(16.W); val length = UInt(16.W)
    val fin = Bool(); val beforeEstablished = Bool()
    val urgent = Bool(); val urgentSequence = UInt(32.W)
}
class TcpControlPacket extends Bundle {
    val command = new TcpSendCommand
    val generation = UInt(32.W)
    val retransmission = Bool()
    val sequenceSpace = Bool()
}
class TcpConnectionCompletion extends Bundle {
    val generation = UInt(32.W)
    val endSequence = UInt(32.W)
    val retransmission = Bool()
}
class TcpAckUpdate extends Bundle {
    val generation = UInt(32.W)
    val previous = UInt(32.W); val acknowledgement = UInt(32.W)
    val window = UInt(32.W)
    val duplicate = Bool(); val windowChanged = Bool(); val handshake = Bool()
}
class TcpConnectionNotice extends Bundle {
    val generation = UInt(32.W)
    // 1 established, 2 peer FIN, 4 closed, 8 peer reset, 16 local abort.
    val flags = UInt(5.W)
}

class TcpConnectionIO extends Bundle {
        val open = Flipped(Decoupled(new TcpConnectionOpen))
        val passiveInitialSequence = Input(UInt(32.W))
        val close = Flipped(Decoupled(Bool())) // true abort, false graceful close
        val rx = Flipped(Decoupled(new TcpConnectionRx))
        val disposition = Decoupled(new TcpRxDisposition)
        val control = Decoupled(new TcpControlPacket)
        val ack = Decoupled(new TcpAckUpdate)
        val notice = Decoupled(new TcpConnectionNotice)
        val sendReserve = Flipped(Decoupled(UInt(16.W)))
        val sendSequence = Output(UInt(32.W))
        val completed = Flipped(Valid(new TcpConnectionCompletion))
        val completionAccepted = Output(Bool())
        val invalidCompletion = Output(Bool())
        val replay = Decoupled(UInt(32.W)) // oldest sequence for data scheduler
        val receiveWindow = Input(UInt(32.W))
        val congestionWindow = Input(UInt(32.W))
        val tick = Input(Bool())
        val state = Output(UInt(4.W)); val generation = Output(UInt(32.W))
        val sendOldest = Output(UInt(32.W)); val sendNext = Output(UInt(32.W)); val sendActual = Output(UInt(32.W))
        val receiveNext = Output(UInt(32.W)); val sendWindow = Output(UInt(32.W))
        val sendCredit = Output(UInt(32.W))
        val dataHeader = Output(new TcpDataHeader)
        val sendMss = Output(UInt(16.W)); val peerScale = Output(UInt(4.W)); val scaleEnabled = Output(Bool())
}

/** One TCB's control plane. Data is retained before rx.fire; the disposition
  * register owns that token until the consumer accepts it. It is therefore
  * safe to ACK only the bytes selected for retention. The application must
  * not see beforeEstablished descriptors until the established notice.
  *
  * This module is not the complete endpoint: a session dispatcher, byte-credit
  * accounting, out-of-order queue, timer adapter and TX descriptor scheduler
  * surround it. receiveWindow is the receive allocator's promised byte window;
  * its right edge must not shrink. congestionWindow comes from congestion
  * control. sendReserve allocates contiguous sequence space, and completions
  * arrive in wire order, only for sequence-consuming packets. Late completions
  * from a previous connection generation cannot mutate this TCB.
  */
class TcpConnectionControl(val timeWaitTicks: Int = 120000, val challengeTicks: Int = 10) extends Module {
    require(timeWaitTicks >= 2 && challengeTicks >= 1)
    val io = IO(new TcpConnectionIO {
        val retry = Flipped(Decoupled(UInt(32.W))) // connection generation
    })
    def after(a: UInt, b: UInt): Bool = { val d = (a - b)(31, 0); d =/= 0.U && !d(31) }
    def ge(a: UInt, b: UInt): Bool = a === b || after(a, b)
    def min(a: UInt, b: UInt): UInt = Mux(a < b, a, b)
    def unicast(ip: UInt): Bool = ip(31, 28) < 14.U && ip(31, 24) =/= 0.U && ip(31, 24) =/= 127.U

    val state = RegInit(TcpState.closed.U(4.W))
    val generation = RegInit(0.U(32.W))
    val config = Reg(new TcpConnectionOpen)
    val remoteIp = Reg(UInt(32.W)); val remotePort = Reg(UInt(16.W))
    val iss = Reg(UInt(32.W)); val irs = Reg(UInt(32.W))
    val oldest = RegInit(0.U(32.W)); val reserved = RegInit(0.U(32.W)); val actual = RegInit(0.U(32.W))
    val receiveNext = RegInit(0.U(32.W))
    val sendWindow = RegInit(0.U(32.W)); val maxSendWindow = RegInit(0.U(32.W))
    val wl1 = Reg(UInt(32.W)); val wl2 = Reg(UInt(32.W))
    val mss = RegInit(536.U(16.W)); val scaleEnabled = RegInit(false.B); val peerScale = RegInit(0.U(4.W))
    val deferredFin = RegInit(false.B)
    val finSequence = Reg(UInt(32.W)); val finOutstanding = RegInit(false.B)
    val timeWait = RegInit(0.U(log2Ceil(timeWaitTicks + 2).W))
    val challenge = RegInit(0.U(log2Ceil(challengeTicks + 1).W))
    val identification = RegInit(0.U(16.W))

    val controlValid = RegInit(false.B); val control = Reg(new TcpControlPacket)
    val dispositionValid = RegInit(false.B); val disposition = Reg(new TcpRxDisposition)
    val ackValid = RegInit(false.B); val ack = Reg(new TcpAckUpdate)
    val noticeValid = RegInit(false.B); val notice = Reg(new TcpConnectionNotice)
    val replayValid = RegInit(false.B); val replay = Reg(UInt(32.W))
    io.control.valid := controlValid; io.control.bits := control
    io.disposition.valid := dispositionValid; io.disposition.bits := disposition
    io.ack.valid := ackValid; io.ack.bits := ack
    io.notice.valid := noticeValid; io.notice.bits := notice
    io.replay.valid := replayValid; io.replay.bits := replay
    when(io.control.fire) { controlValid := false.B }
    when(io.disposition.fire) { dispositionValid := false.B }
    when(io.ack.fire) { ackValid := false.B }
    when(io.notice.fire) { noticeValid := false.B }
    when(io.replay.fire) { replayValid := false.B }
    val idle = !controlValid && !dispositionValid && !ackValid && !noticeValid && !replayValid

    val matchingCompletion = io.completed.valid && io.completed.bits.generation === generation &&
        state =/= TcpState.closed.U && state =/= TcpState.listen.U
    val newCompletion = matchingCompletion && !io.completed.bits.retransmission &&
        after(io.completed.bits.endSequence, actual) && ge(reserved, io.completed.bits.endSequence)
    val retryCompletion = matchingCompletion && io.completed.bits.retransmission &&
        after(io.completed.bits.endSequence, oldest) && ge(actual, io.completed.bits.endSequence)
    val retiredCompletion = matchingCompletion && io.completed.bits.retransmission &&
        ge(oldest, io.completed.bits.endSequence) && ge(actual, io.completed.bits.endSequence)
    val actualNow = Mux(newCompletion, io.completed.bits.endSequence, actual)
    io.completionAccepted := newCompletion || retryCompletion
    io.invalidCompletion := matchingCompletion && !newCompletion && !retryCompletion && !retiredCompletion
    when(newCompletion) { actual := io.completed.bits.endSequence }

    val localScale = Mux(scaleEnabled, config.localScale, 0.U)
    val advertised = min(io.receiveWindow >> localScale, 65535.U)(15, 0)
    val receiveWindow = (advertised.pad(32) << localScale)(31, 0)
    val synWindow = min(io.receiveWindow, 65535.U)(15, 0)

    def emit(flags: UInt, seq: UInt, acknowledgment: UInt, window: UInt,
             dstIp: UInt, dstPort: UInt, srcIp: UInt, srcPort: UInt,
             useScale: Bool, retry: Bool, epoch: UInt): Unit = {
        controlValid := true.B
        control := 0.U.asTypeOf(new TcpControlPacket)
        control.generation := epoch; control.retransmission := retry
        control.sequenceSpace := flags(1) || flags(0)
        control.command.sourceIp := srcIp; control.command.destinationIp := dstIp
        control.command.sourcePort := srcPort; control.command.destinationPort := dstPort
        control.command.sequence := seq; control.command.acknowledgement := acknowledgment
        control.command.flags := flags; control.command.window := window
        control.command.identification := identification; identification := identification + 1.U
        control.command.ttl := 64.U
        when(flags(1)) {
            control.command.optionLength := Mux(useScale, 8.U, 4.U)
            val options = Seq(2.U(8.W), 4.U(8.W), config.localMss(15, 8), config.localMss(7, 0),
                1.U(8.W), 3.U(8.W), 3.U(8.W), config.localScale.pad(8))
            control.command.options := options.zipWithIndex.map { case (b, i) =>
                (b.pad(320) << (8 * i))(319, 0)
            }.reduce(_ | _)
        }
    }
    def connected(flags: Int, seq: UInt, acknowledgment: UInt, window: UInt, retry: Bool = false.B): Unit =
        emit(flags.U(8.W), seq, acknowledgment, window, remoteIp, remotePort,
            config.localIp, config.localPort, scaleEnabled, retry, generation)
    def notify(flags: UInt): Unit = {
        noticeValid := true.B; notice.generation := generation; notice.flags := flags
    }
    def challengeAck(): Unit = when(challenge === 0.U) {
        connected(16, actualNow, receiveNext, advertised)
        challenge := challengeTicks.U
    }
    def enterTimeWait(): Unit = { state := TcpState.timeWait.U; timeWait := (timeWaitTicks + 1).U }
    def acceptedAck(number: UInt, newWindow: UInt, duplicate: Bool, handshake: Bool): Unit = {
        ackValid := true.B; ack.generation := generation
        ack.previous := oldest; ack.acknowledgement := number; ack.window := newWindow
        ack.duplicate := duplicate; ack.windowChanged := newWindow =/= sendWindow; ack.handshake := handshake
        when(after(number, oldest)) { oldest := number }
    }

    io.open.ready := idle && state === TcpState.closed.U
    val canClose = io.close.bits || state =/= TcpState.established.U && state =/= TcpState.closeWait.U ||
        actualNow === reserved
    io.close.ready := idle && !io.open.fire && canClose
    io.rx.ready := idle && !io.open.fire && !io.close.fire
    io.retry.ready := idle && !io.open.fire && !io.close.fire && !io.rx.valid
    val flight = reserved - oldest
    val limit = min(sendWindow, io.congestionWindow)
    val available = Mux(limit > flight, limit - flight, 0.U)
    io.sendReserve.ready := idle && !io.open.fire && !io.close.fire && !io.rx.valid && !io.retry.valid &&
        (state === TcpState.established.U || state === TcpState.closeWait.U) &&
        io.sendReserve.bits =/= 0.U && io.sendReserve.bits <= mss && io.sendReserve.bits <= available
    io.sendCredit := available
    io.sendSequence := reserved
    when(io.sendReserve.fire) { reserved := reserved + io.sendReserve.bits }

    when(io.tick && challenge =/= 0.U) { challenge := challenge - 1.U }
    when(io.tick && state === TcpState.timeWait.U && timeWait > 1.U) { timeWait := timeWait - 1.U }
    when(io.tick && state === TcpState.timeWait.U && timeWait === 1.U && idle && !io.rx.fire && !io.close.fire) {
        state := TcpState.closed.U; timeWait := 0.U; notify(4.U)
    }

    when(io.open.fire) {
        config := io.open.bits
        config.localMss := min(Mux(io.open.bits.localMss === 0.U, 1.U, io.open.bits.localMss), 65495.U)
        config.localScale := min(io.open.bits.localScale, 14.U)
        remoteIp := io.open.bits.remoteIp; remotePort := io.open.bits.remotePort
        generation := generation + 1.U
        iss := io.open.bits.initialSequence
        oldest := io.open.bits.initialSequence; actual := io.open.bits.initialSequence
        reserved := io.open.bits.initialSequence
        receiveNext := 0.U; sendWindow := 0.U; maxSendWindow := 0.U
        scaleEnabled := false.B; peerScale := 0.U; deferredFin := false.B; finOutstanding := false.B
        mss := min(Mux(io.open.bits.localMss === 0.U, 1.U, io.open.bits.localMss), 536.U)
        challenge := 0.U
        val valid = unicast(io.open.bits.localIp) && io.open.bits.localPort =/= 0.U &&
            (io.open.bits.passive || unicast(io.open.bits.remoteIp) && io.open.bits.remotePort =/= 0.U)
        when(!valid) {
            state := TcpState.closed.U
            notify(4.U); notice.generation := generation + 1.U
        }.elsewhen(io.open.bits.passive) { state := TcpState.listen.U }
            .otherwise {
                state := TcpState.synSent.U; reserved := io.open.bits.initialSequence + 1.U
                emit(2.U(8.W), io.open.bits.initialSequence, 0.U, synWindow,
                    io.open.bits.remoteIp, io.open.bits.remotePort, io.open.bits.localIp, io.open.bits.localPort,
                    io.open.bits.offerScale, false.B, generation + 1.U)
                // OPEN is the only emit whose configuration is not registered yet.
                val localMss = min(Mux(io.open.bits.localMss === 0.U, 1.U, io.open.bits.localMss), 65495.U)
                val scale = min(io.open.bits.localScale, 14.U).pad(8)
                control.command.options := 2.U(320.W) | (4.U(320.W) << 8) |
                    (localMss(15, 8).pad(320) << 16) | (localMss(7, 0).pad(320) << 24) |
                    (1.U(320.W) << 32) | (3.U(320.W) << 40) | (3.U(320.W) << 48) | (scale.pad(320) << 56)
            }
    }
    when(io.close.fire) {
        when(io.close.bits) {
            when(state =/= TcpState.closed.U && state =/= TcpState.listen.U && state =/= TcpState.synSent.U) {
                connected(20, actualNow, receiveNext, 0.U)
            }
            state := TcpState.closed.U; finOutstanding := false.B; notify(20.U)
        }.elsewhen(state === TcpState.established.U || state === TcpState.closeWait.U) {
            finSequence := reserved; finOutstanding := true.B; reserved := reserved + 1.U
            connected(17, reserved, receiveNext, advertised)
            state := Mux(state === TcpState.closeWait.U, TcpState.lastAck.U, TcpState.finWait1.U)
        }.elsewhen(state === TcpState.listen.U || state === TcpState.synSent.U) {
            state := TcpState.closed.U; notify(4.U)
        }
        // A graceful close during SYN-RECEIVED is deferred by ready below.
    }
    // SYN-RECEIVED must finish the handshake before assigning a FIN.
    when(state === TcpState.synReceived.U && !io.close.bits) { io.close.ready := false.B }

    when(io.retry.fire && io.retry.bits === generation && oldest =/= actualNow) {
        when(state === TcpState.synSent.U || state === TcpState.synReceived.U) {
            val flags = Mux(state === TcpState.synSent.U, 2.U(8.W), 18.U(8.W))
            emit(flags, iss, Mux(state === TcpState.synSent.U, 0.U, receiveNext), synWindow,
                remoteIp, remotePort, config.localIp, config.localPort,
                Mux(state === TcpState.synSent.U, config.offerScale, scaleEnabled), true.B, generation)
        }.elsewhen(finOutstanding && oldest === finSequence) {
            connected(17, finSequence, receiveNext, advertised, true.B)
        }.elsewhen(state =/= TcpState.closed.U && state =/= TcpState.listen.U && state =/= TcpState.timeWait.U) {
            replayValid := true.B; replay := oldest
        }
    }

    val rx = io.rx.bits.segment
    val syn = rx.flags(1); val rst = rx.flags(2); val hasAck = rx.flags(4); val fin = rx.flags(0)
    val segLength = rx.payloadLength +& syn.asUInt +& fin.asUInt
    val sequenceEnd = rx.sequence + segLength
    val listenMatch = rx.destinationIp === config.localIp && rx.destinationPort === config.localPort &&
        (config.remoteIp === 0.U || rx.sourceIp === config.remoteIp) &&
        (config.remotePort === 0.U || rx.sourcePort === config.remotePort)
    val establishedMatch = rx.destinationIp === config.localIp && rx.destinationPort === config.localPort &&
        rx.sourceIp === remoteIp && rx.sourcePort === remotePort
    val matches = Mux(state === TcpState.closed.U, true.B,
        Mux(state === TcpState.listen.U, listenMatch, establishedMatch))
    val firstIn = (rx.sequence - receiveNext)(31, 0) < receiveWindow
    val lastIn = (sequenceEnd - 1.U - receiveNext)(31, 0) < receiveWindow
    val sequenceAcceptable = Mux(segLength === 0.U,
        Mux(receiveWindow === 0.U, rx.sequence === receiveNext, firstIn),
        receiveWindow =/= 0.U && (firstIn || lastIn))
    val newPeerMss = min(config.localMss, Mux(rx.options.mssPresent && rx.options.mss =/= 0.U, rx.options.mss, 536.U))
    val negotiateScale = config.offerScale && rx.options.scalePresent
    def resetReply(): Unit = emit(Mux(hasAck, 4.U(8.W), 20.U(8.W)), Mux(hasAck, rx.acknowledgement, 0.U),
        Mux(hasAck, 0.U, sequenceEnd), 0.U, rx.sourceIp, rx.sourcePort, rx.destinationIp, rx.destinationPort,
        false.B, false.B, generation)

    // Return selected durable bytes, FIN acceptance, and resulting RCV.NXT.
    def body(start: UInt, base: UInt, window: UInt): (UInt, UInt, Bool, UInt) = {
        val skip = min(Mux(after(base, start), base - start, 0.U), rx.payloadLength)(15, 0)
        val contiguous = (start + skip) === base
        val kept = Mux(contiguous && io.rx.bits.payloadStored, min(rx.payloadLength - skip, window), 0.U)(15, 0)
        val finSeq = start + rx.payloadLength
        val acceptFin = fin && contiguous && (skip +& kept) === rx.payloadLength &&
            finSeq === base + kept && (finSeq - base)(31, 0) < window
        (skip, kept, acceptFin, (base + kept + acceptFin.asUInt)(31, 0))
    }
    def retain(skip: UInt, length: UInt, acceptFin: Bool, beforeEstablished: Bool): Unit = {
        disposition.keep := length =/= 0.U; disposition.skip := skip; disposition.length := length
        disposition.fin := acceptFin; disposition.beforeEstablished := beforeEstablished
        disposition.urgent := rx.flags(5) && rx.urgent =/= 0.U && length =/= 0.U
        disposition.urgentSequence := rx.sequence + rx.urgent
    }
    def peerSyn(): Unit = {
        irs := rx.sequence; remoteIp := rx.sourceIp; remotePort := rx.sourcePort
        scaleEnabled := negotiateScale; peerScale := Mux(negotiateScale, min(rx.options.scale, 14.U), 0.U)
        mss := newPeerMss; sendWindow := rx.window; maxSendWindow := rx.window
        wl1 := rx.sequence; wl2 := rx.acknowledgement
    }

    when(io.rx.fire) {
        dispositionValid := true.B
        disposition := 0.U.asTypeOf(new TcpRxDisposition)
        disposition.generation := generation; disposition.token := io.rx.bits.payloadToken
        when(rx.errors === 0.U && matches && unicast(rx.sourceIp) && unicast(rx.destinationIp)) {
            when(state === TcpState.closed.U) {
                when(!rst) { resetReply() }
            }.elsewhen(state === TcpState.listen.U) {
                when(!rst && hasAck) { resetReply() }
                    .elsewhen(!rst && syn) {
                        peerSyn()
                        iss := io.passiveInitialSequence; oldest := io.passiveInitialSequence
                        actual := io.passiveInitialSequence; reserved := io.passiveInitialSequence + 1.U
                        val (skip, kept, acceptedFin, next) = body(rx.sequence + 1.U, rx.sequence + 1.U, synWindow)
                        retain(skip, kept, acceptedFin, true.B)
                        receiveNext := next; deferredFin := acceptedFin
                        state := TcpState.synReceived.U
                        emit(18.U(8.W), io.passiveInitialSequence, next, synWindow - kept,
                            rx.sourceIp, rx.sourcePort, config.localIp, config.localPort,
                            negotiateScale, false.B, generation)
                    }
            }.elsewhen(state === TcpState.synSent.U) {
                val ackOk = hasAck && after(rx.acknowledgement, iss) && ge(actualNow, rx.acknowledgement)
                when(hasAck && !ackOk) { when(!rst) { resetReply() } }
                    .elsewhen(rst) { when(ackOk) { state := TcpState.closed.U; notify(12.U) } }
                    .elsewhen(syn) {
                        peerSyn()
                        val (skip, kept, acceptedFin, next) = body(rx.sequence + 1.U, rx.sequence + 1.U, synWindow)
                        retain(skip, kept, acceptedFin, !ackOk)
                        receiveNext := next; deferredFin := acceptedFin && !ackOk
                        when(ackOk) {
                            acceptedAck(rx.acknowledgement, rx.window, false.B, true.B)
                            state := Mux(acceptedFin, TcpState.closeWait.U, TcpState.established.U)
                            notify(Mux(acceptedFin, 3.U, 1.U))
                            // SYN windows are never scaled, even if WS was negotiated.
                            emit(16.U(8.W), actualNow, next,
                                min((io.receiveWindow - kept) >> Mux(negotiateScale, config.localScale, 0.U), 65535.U),
                                rx.sourceIp, rx.sourcePort, config.localIp, config.localPort, false.B, false.B, generation)
                        }.otherwise {
                            state := TcpState.synReceived.U
                            emit(18.U(8.W), iss, next, synWindow - kept, rx.sourceIp, rx.sourcePort,
                                config.localIp, config.localPort, negotiateScale, true.B, generation)
                        }
                    }
            }.otherwise {
                val duplicateSyn = state === TcpState.synReceived.U && syn && !rst && rx.sequence === irs
                val simultaneousAck = duplicateSyn && hasAck && rx.acknowledgement === iss + 1.U &&
                    ge(actualNow, rx.acknowledgement)
                val duplicateFin = state === TcpState.timeWait.U && fin && !rst && !syn && sequenceEnd === receiveNext
                when(rst) {
                    when(state =/= TcpState.timeWait.U) {
                        when(rx.sequence === receiveNext) {
                            when(state === TcpState.synReceived.U && config.passive) {
                                state := TcpState.listen.U; generation := generation + 1.U
                                oldest := iss; actual := iss; reserved := iss
                                scaleEnabled := false.B; peerScale := 0.U; deferredFin := false.B
                            }.otherwise { state := TcpState.closed.U }
                            finOutstanding := false.B; notify(12.U)
                        }.elsewhen(firstIn) { challengeAck() }
                    }
                }.elsewhen(duplicateSyn && !hasAck) {
                    connected(18, iss, receiveNext, synWindow, true.B)
                }.elsewhen(duplicateFin) {
                    connected(16, actualNow, receiveNext, advertised); timeWait := (timeWaitTicks + 1).U
                }.elsewhen(syn && !simultaneousAck) { challengeAck() }
                    .elsewhen(!sequenceAcceptable && !simultaneousAck) {
                        connected(16, actualNow, receiveNext, advertised)
                    }.elsewhen(hasAck) {
                        // Test the bounded modular interval directly. Two signed
                        // comparisons alone mishandle an exactly half-space ACK
                        // when MAX.SND.WND is zero.
                        val ackLower = oldest - maxSendWindow
                        val ackInRange = (rx.acknowledgement - ackLower)(31, 0) <= (actualNow - ackLower)(31, 0)
                        when(!ackInRange) { challengeAck() }
                            .elsewhen(state === TcpState.synReceived.U && !after(rx.acknowledgement, oldest)) { resetReply() }
                            .otherwise {
                                val opening = state === TcpState.synReceived.U
                                val updateWindow = ge(rx.acknowledgement, oldest) &&
                                    (opening || after(rx.sequence, wl1) || rx.sequence === wl1 && ge(rx.acknowledgement, wl2))
                                val peerWindow = Mux(syn, rx.window.pad(32), (rx.window.pad(32) << peerScale)(31, 0))
                                val nextWindow = Mux(updateWindow, peerWindow, sendWindow)
                                when(updateWindow) {
                                    sendWindow := peerWindow; wl1 := rx.sequence; wl2 := rx.acknowledgement
                                    when(peerWindow > maxSendWindow) { maxSendWindow := peerWindow }
                                }
                                when(ge(rx.acknowledgement, oldest)) {
                                    acceptedAck(rx.acknowledgement, nextWindow,
                                        rx.acknowledgement === oldest && oldest =/= actualNow &&
                                        rx.payloadLength === 0.U && !syn && !fin && nextWindow === sendWindow, opening)
                                }
                                val finAcked = finOutstanding && ge(rx.acknowledgement, finSequence + 1.U)
                                when(finAcked) { finOutstanding := false.B }
                                val canReceive = state === TcpState.established.U || state === TcpState.finWait1.U ||
                                    state === TcpState.finWait2.U || opening && !deferredFin
                                val (skip, count, takeFin, newNext) = body(rx.sequence + simultaneousAck.asUInt, receiveNext, receiveWindow)
                                val kept = Mux(canReceive, count, 0.U)
                                val gotFin = canReceive && takeFin
                                when(canReceive) {
                                    retain(skip, kept, gotFin, false.B)
                                    receiveNext := newNext
                                }
                                val nextAck = Mux(canReceive, newNext, receiveNext)
                                when(rx.payloadLength =/= 0.U || fin || simultaneousAck) {
                                    connected(16, actualNow, nextAck, min((receiveWindow - kept) >> localScale, 65535.U))
                                }
                                when(opening) {
                                    state := Mux(gotFin || deferredFin, TcpState.closeWait.U, TcpState.established.U)
                                    notify(Mux(gotFin || deferredFin, 3.U, 1.U)); deferredFin := false.B
                                }.elsewhen(state === TcpState.established.U && gotFin) {
                                    state := TcpState.closeWait.U; notify(2.U)
                                }.elsewhen(state === TcpState.finWait1.U) {
                                    when(gotFin && finAcked) { enterTimeWait(); notify(2.U) }
                                        .elsewhen(gotFin) { state := TcpState.closing.U; notify(2.U) }
                                        .elsewhen(finAcked) { state := TcpState.finWait2.U }
                                }.elsewhen(state === TcpState.finWait2.U && gotFin) { enterTimeWait(); notify(2.U) }
                                    .elsewhen(state === TcpState.closing.U && finAcked) { enterTimeWait() }
                                    .elsewhen(state === TcpState.lastAck.U && finAcked) { state := TcpState.closed.U; notify(4.U) }
                            }
                    }
            }
        }
    }
    io.state := state; io.generation := generation
    io.sendOldest := oldest; io.sendNext := reserved; io.sendActual := actual
    io.receiveNext := receiveNext; io.sendWindow := sendWindow; io.sendMss := mss
    io.peerScale := peerScale; io.scaleEnabled := scaleEnabled
    io.dataHeader.sourceIp := config.localIp; io.dataHeader.destinationIp := remoteIp
    io.dataHeader.sourcePort := config.localPort; io.dataHeader.destinationPort := remotePort
    io.dataHeader.acknowledgement := receiveNext; io.dataHeader.window := advertised
    io.dataHeader.ttl := 64.U; io.dataHeader.tos := 0.U
}
