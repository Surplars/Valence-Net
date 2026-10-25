package netstack

import chisel3._
import chisel3.util._

/** Single-session IPv4/TCP protocol core. Ethernet routing/MAC precede/follow
  * these IP streams. completed is the ordered physical result defined by
  * TcpConnectionTransmit, not the IP output handshake. Applications consume
  * received bytes in order; output.last delimits a chunk, not a peer packet.
  * Local IP configuration is fixed while active. Congestion policy and ISNs
  * remain explicit inputs to this core.
  */
class TcpIpv4Endpoint(val txBanks: Int = 8, val txBeats: Int = 128,
    val rxWords: Int = 4096, val packetBeats: Int = 128,
    val ticksPerSecond: Int = 1000, val timeWaitTicks: Int = 120000,
    val challengeTicks: Int = 10, val maxRetries: Int = 8,
    val completionDepth: Int = 16, val autonomousCongestion: Boolean = true, val sackSupport: Boolean = true,
    val deferOpenClear: Boolean = false, val pipelineCompletions: Boolean = false,
    val closedResponses: Boolean = true, val sharedReceive: Boolean = false) extends Module {
    val io = IO(new Bundle {
        val localIp = Input(UInt(32.W)); val netmask = Input(UInt(32.W))
        val pathMtuLimit = Input(UInt(16.W))
        val icmpError = Flipped(Valid(new IcmpErrorEvent))
        val networkError = Valid(new TcpNetworkError)
        val pathMtu = Output(UInt(16.W))
        val exceptionOut = Decoupled(new PacketBeat(16))
        val exceptionDestinationIp = Output(UInt(32.W))
        val open = Flipped(Decoupled(new TcpConnectionOpen))
        val passiveInitialSequence = Input(UInt(32.W))
        val close = Flipped(Decoupled(Bool()))
        val tick = Input(Bool())
        val congestionWindow = Input(UInt(32.W))
        val reorderCount = Output(UInt(4.W)); val reorderDropped = Output(Bool())
        val congestionLimit = Output(UInt(32.W)); val congestionThreshold = Output(UInt(32.W)); val fastRecovery = Output(Bool())
        val notice = Decoupled(new TcpConnectionNotice)
        val allocate = Flipped(Decoupled(new TcpStoreAllocation))
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val stored = Decoupled(new TcpStoreResult)
        val received = Decoupled(new PacketBeat(16))
        val rx = Flipped(Decoupled(new PacketBeat(16)))
        val validated = if (sharedReceive) Some(new Bundle {
            val metadata = Flipped(Decoupled(new TcpSegment))
            val generation = Input(UInt(32.W))
            val payload = Flipped(Decoupled(new PacketBeat(16)))
        }) else None
        val out = Decoupled(new PacketBeat(16))
        val packet = Output(new TcpWireDescriptor)
        val completed = Flipped(Valid(new TcpWireResult))
        val invalidCompletion = Output(Bool())
        val completionAccepted = Output(Bool())
        val fault = Output(Bool()); val rxDrops = Output(UInt(2.W))
        val rxConsumed = Output(Bool())
        val ownershipDrained = Output(Bool())
        val timeout = Valid(UInt(32.W))
        val state = Output(UInt(4.W)); val generation = Output(UInt(32.W))
        val sendOldest = Output(UInt(32.W)); val sendNext = Output(UInt(32.W))
        val sendActual = Output(UInt(32.W)); val receiveNext = Output(UInt(32.W))
        val sendMss = Output(UInt(16.W)); val receiveWindow = Output(UInt(32.W))
        val freeBanks = Output(UInt(log2Ceil(txBanks + 1).W))
        val outstanding = Output(UInt(log2Ceil(completionDepth + 1).W))
        val timerRunning = Output(Bool()); val timerExpired = Output(Bool())
        val rto = Output(UInt(32.W)); val retries = Output(UInt(8.W))
    })
    val receive = Module(new TcpReceiveQueue(rxWords))
    val connection = Module(new TcpConnectionTransmit(txBanks, txBeats, ticksPerSecond,
        timeWaitTicks, challengeTicks, maxRetries, completionDepth, autonomousCongestion, receiveReorder = true, sackSupport = sackSupport,
        pipelineCompletions = pipelineCompletions))
    val receiveDrops = WireDefault(0.U(2.W))
    val ingressEmpty = WireDefault(true.B)
    if (sharedReceive) {
        require(!closedResponses, "shared dispatcher owns stateless responses")
        io.rx.ready := false.B
        val ingress = Module(new TcpValidatedReceiveBuffer(packetBeats))
        ingress.io.inMetadata.valid := io.validated.get.metadata.valid
        ingress.io.inMetadata.bits.segment := io.validated.get.metadata.bits
        ingress.io.inMetadata.bits.generation := io.validated.get.generation
        io.validated.get.metadata.ready := ingress.io.inMetadata.ready
        ingress.io.in <> io.validated.get.payload
        ingress.io.generation := connection.io.generation
        receive.io.metadata <> ingress.io.metadata
        receive.io.payload <> ingress.io.out
        receiveDrops := ingress.io.dropped; ingressEmpty := ingress.io.empty
        io.exceptionOut.valid := false.B; io.exceptionOut.bits := 0.U.asTypeOf(new PacketBeat(16))
        io.exceptionDestinationIp := 0.U
    } else {
        val ip = Module(new Ipv4Receive(packetBeats))
        val tcp = Module(new TcpReceive(packetBeats))
        ip.io.in <> io.rx
        ip.io.localIp := io.localIp
        ip.io.acceptBroadcast := false.B
        val selected = ip.io.protocol === 6.U
        tcp.io.in.valid := ip.io.out.valid && selected
        tcp.io.in.bits := ip.io.out.bits
        ip.io.out.ready := !selected || tcp.io.in.ready
        tcp.io.sourceIp := ip.io.source; tcp.io.destinationIp := ip.io.destination
        tcp.io.ipPayloadLength := ip.io.payloadLength
        if (closedResponses) {
            val responder = Module(new TcpResetTransmit(challengeTicks))
            responder.io.localIp := io.localIp; responder.io.netmask := io.netmask; responder.io.tick := io.tick
            io.exceptionOut <> responder.io.out; io.exceptionDestinationIp := responder.io.destinationIp
            val listener = RegEnable(io.open.bits, io.open.fire)
            val seg = tcp.io.metadata.bits
            val tuple = connection.io.pathTuple
            val exact = seg.sourceIp === tuple.remoteIp && seg.destinationIp === tuple.localIp &&
                seg.sourcePort === tuple.remotePort && seg.destinationPort === tuple.localPort
            val listening = connection.io.state === TcpState.listen.U &&
                seg.destinationIp === io.localIp && seg.destinationPort === listener.localPort &&
                (listener.remoteIp === 0.U || seg.sourceIp === listener.remoteIp) &&
                (listener.remotePort === 0.U || seg.sourcePort === listener.remotePort)
            val reserved = connection.io.state =/= TcpState.closed.U || !connection.io.ownershipDrained
            val known = (reserved && connection.io.state =/= TcpState.listen.U && exact) || listening
            val heldOld = connection.io.state === TcpState.closed.U && !connection.io.ownershipDrained && exact
            val unknown = !known
            responder.io.in.valid := tcp.io.metadata.valid && unknown && !connection.io.open.fire
            responder.io.in.bits := seg
            receive.io.metadata.valid := tcp.io.metadata.valid && known && !heldOld
            receive.io.metadata.bits := seg
            tcp.io.metadata.ready := Mux(unknown, responder.io.in.ready,
                heldOld || receive.io.metadata.ready)
            val discard = RegEnable(unknown || heldOld, tcp.io.metadata.fire)
            receive.io.payload.valid := tcp.io.payload.valid && !discard
            receive.io.payload.bits := tcp.io.payload.bits
            tcp.io.payload.ready := discard || receive.io.payload.ready
        } else {
            receive.io.metadata <> tcp.io.metadata; receive.io.payload <> tcp.io.payload
            io.exceptionOut.valid := false.B; io.exceptionOut.bits := 0.U.asTypeOf(new PacketBeat(16))
            io.exceptionDestinationIp := 0.U
        }
        receiveDrops := PopCount(VecInit(Seq(ip.io.dropped, tcp.io.dropped,
            ip.io.out.fire && ip.io.out.bits.last && !selected)))
    }
    connection.io.rx <> receive.io.rx;
    io.rxConsumed := connection.io.rxConsumed
    io.ownershipDrained := connection.io.ownershipDrained && receive.io.empty && ingressEmpty
    receive.io.disposition <> connection.io.disposition
    receive.io.receiveNext := connection.io.receiveNext
    receive.io.generation := connection.io.generation
    receive.io.credit := connection.io.receiveCredit
    receive.io.enabled := connection.io.state === TcpState.established.U ||
        connection.io.state === TcpState.finWait1.U || connection.io.state === TcpState.finWait2.U
    connection.io.receiveWindow := receive.io.window
    connection.io.updateWindow := receive.io.updateWindow
    connection.io.receiveSack := receive.io.sack
    connection.io.ack.ready := true.B
    io.received <> receive.io.out
    io.reorderCount := receive.io.reorderCount; io.reorderDropped := receive.io.reorderDropped
    // Graceful protocol close does not consume unread application bytes.
    // Only reset/abort or an accepted new OPEN cancels receive ownership.
    val aborted = connection.io.notice.valid &&
        (connection.io.notice.bits.flags(3) || connection.io.notice.bits.flags(4))
    val establishedDelivery = RegInit(false.B)
    when(connection.io.state >= TcpState.established.U) { establishedDelivery := true.B }
    when(aborted || connection.io.open.fire) { establishedDelivery := false.B }
    // Deferred clearing requires a parent that drains admission/validation
    // before OPEN. A newly admitted packet cannot reach RX before this edge.
    val openClear = if (deferOpenClear) RegNext(connection.io.open.fire, false.B) else connection.io.open.fire
    receive.io.clear := aborted || openClear
    receive.io.deliver := establishedDelivery || connection.io.state >= TcpState.established.U
    connection.io.open.valid := io.open.valid && receive.io.empty && ingressEmpty
    connection.io.open.bits := io.open.bits
    connection.io.open.bits.localIp := io.localIp
    io.open.ready := connection.io.open.ready && receive.io.empty && ingressEmpty
    connection.io.passiveInitialSequence := io.passiveInitialSequence
    connection.io.pathMtuLimit := io.pathMtuLimit
    val feedback = Module(new TcpIcmpFeedback(ticksPerSecond))
    feedback.io.error := io.icmpError; feedback.io.tuple := connection.io.pathTuple
    feedback.io.state := connection.io.state; feedback.io.generation := connection.io.generation
    feedback.io.oldest := connection.io.sendOldest; feedback.io.actual := connection.io.sendActual
    feedback.io.reserved := connection.io.sendNext
    feedback.io.pathMtu := connection.io.pathMtu; feedback.io.limit := io.pathMtuLimit
    feedback.io.tick := io.tick; feedback.io.clear := connection.io.open.fire
    connection.io.pathMtuUpdate := feedback.io.update
    io.networkError := feedback.io.notice; io.pathMtu := connection.io.pathMtu
    connection.io.close <> io.close
    connection.io.tick := io.tick
    connection.io.congestionWindow := io.congestionWindow
    io.notice <> connection.io.notice
    connection.io.allocate <> io.allocate; connection.io.in <> io.in; io.stored <> connection.io.stored
    io.out <> connection.io.out; io.packet := connection.io.packet
    connection.io.completed <> io.completed
    io.invalidCompletion := connection.io.invalidCompletion; io.completionAccepted := connection.io.completionAccepted
    // Diagnostic events cross a register before the shared multi-core OR.
    // No protocol handshake, abort policy or payload validation uses this port.
    io.fault := RegNext(connection.io.fault || receive.io.fault, false.B)
    io.rxDrops := receiveDrops
    io.timeout <> connection.io.timeout
    io.congestionLimit := connection.io.congestionLimit; io.congestionThreshold := connection.io.congestionThreshold; io.fastRecovery := connection.io.fastRecovery
    io.state := connection.io.state; io.generation := connection.io.generation
    io.sendOldest := connection.io.sendOldest; io.sendNext := connection.io.sendNext
    io.sendActual := connection.io.sendActual; io.receiveNext := connection.io.receiveNext
    io.sendMss := connection.io.sendMss; io.receiveWindow := receive.io.window
    io.freeBanks := connection.io.freeBanks; io.outstanding := connection.io.outstanding
    io.timerRunning := connection.io.timerRunning; io.timerExpired := connection.io.timerExpired
    io.rto := connection.io.rto; io.retries := connection.io.retries
}
