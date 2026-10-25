package netstack

import chisel3._
import chisel3.util._

/** An explicit port per connection. The vector index is the connection ID;
  * each port has independent flow control, receive credit and notifications.
  */
class MultiTcpApplicationPort(val txBanks: Int, val completionDepth: Int) extends Bundle {
    val pathMtu = Output(UInt(16.W))
    val networkError = Valid(new TcpNetworkError)
    val open = Flipped(Decoupled(new TcpConnectionOpen))
    val passiveInitialSequence = Input(UInt(32.W))
    val close = Flipped(Decoupled(Bool()))
    val congestionWindow = Input(UInt(32.W))
    val notice = Decoupled(new TcpConnectionNotice)
    val allocate = Flipped(Decoupled(new TcpStoreAllocation))
    val in = Flipped(Decoupled(new PacketBeat(16)))
    val stored = Decoupled(new TcpStoreResult)
    val received = Decoupled(new PacketBeat(16))
    // A conflicting active OPEN is not consumed; withdraw or change the tuple.
    val openConflict = Output(Bool())
    val congestionLimit = Output(UInt(32.W)); val congestionThreshold = Output(UInt(32.W))
    val fastRecovery = Output(Bool()); val timeout = Valid(UInt(32.W))
    val state = Output(UInt(4.W)); val generation = Output(UInt(32.W))
    val sendOldest = Output(UInt(32.W)); val sendNext = Output(UInt(32.W))
    val sendActual = Output(UInt(32.W)); val receiveNext = Output(UInt(32.W))
    val sendMss = Output(UInt(16.W)); val receiveWindow = Output(UInt(32.W))
    val freeBanks = Output(UInt(log2Ceil(txBanks + 1).W))
    val outstanding = Output(UInt(log2Ceil(completionDepth + 1).W))
    val timerRunning = Output(Bool()); val timerExpired = Output(Bool())
    val rto = Output(UInt(32.W)); val retries = Output(UInt(8.W))
    val reorderCount = Output(UInt(4.W)); val reorderDropped = Output(Bool())
    val completionAccepted = Output(Bool())
}

/** Bounded parallel connection contexts with one 128-bit IP input/output.
  *
  * The dispatcher captures three header beats. One shared IPv4/TCP validator
  * checks complete packets before routing to independent committed RX banks. A SYN
  * temporarily reserves a listener; a rejected packet releases that reservation.
  * Different listeners may share a local port and have optional peer filters.
  *
  * Unknown tuples use a bounded, rate-limited, fully validated reset responder.
  * Unsupported headers and busy/reserved receive contexts are silently dropped.
  * A busy context does not hold the common ingress waiting for application space.
  *
  * Physical completions are GLOBAL submission order. packetConnection identifies
  * the owner, while globally allocated tag + the owner's generation identify the
  * callback. The global ledger translates tags back to each endpoint's ledger.
  * Reset must also reset the downstream MAC/completion adapter.
  */
class MultiTcpIpv4Endpoint(val connectionCount: Int = 4, val txBanks: Int = 8,
    val txBeats: Int = 128, val rxWords: Int = 4096, val packetBeats: Int = 128,
    val ticksPerSecond: Int = 1000, val timeWaitTicks: Int = 120000,
    val challengeTicks: Int = 10, val maxRetries: Int = 8,
    val completionDepth: Int = 16, val autonomousCongestion: Boolean = true) extends Module {
    require(connectionCount >= 1 && connectionCount <= 8)
    val connectionBits = math.max(1, log2Ceil(connectionCount))
    val globalDepth = connectionCount * completionDepth
    val io = IO(new Bundle {
        val localIp = Input(UInt(32.W))
        val pathMtuLimit = Input(UInt(16.W))
        val icmpError = Flipped(Valid(new IcmpErrorEvent)); val netmask = Input(UInt(32.W)); val tick = Input(Bool())
        val connections = Vec(connectionCount, new MultiTcpApplicationPort(txBanks, completionDepth))
        val rx = Flipped(Decoupled(new PacketBeat(16)))
        val out = Decoupled(new PacketBeat(16))
        val exceptionOut = Decoupled(new PacketBeat(16))
        val exceptionDestinationIp = Output(UInt(32.W))
        val packet = Output(new TcpWireDescriptor)
        val packetConnection = Output(UInt(connectionBits.W))
        val completed = Flipped(Valid(new TcpWireResult))
        val completionAccepted = Output(Bool()); val invalidCompletion = Output(Bool())
        val outstanding = Output(UInt(log2Ceil(globalDepth + 1).W))
        val rxDropped = Output(Bool()); val rxBusyDropped = Output(Bool())
        val rxDrops = Output(UInt(log2Ceil(2 * connectionCount + 6).W))
        val fault = Output(Bool())
    })
    val cores = Seq.fill(connectionCount)(Module(new TcpIpv4Endpoint(txBanks, txBeats,
        rxWords, packetBeats, ticksPerSecond, timeWaitTicks, challengeTicks,
        maxRetries, completionDepth, autonomousCongestion,
        deferOpenClear = true, pipelineCompletions = true, closedResponses = false, sharedReceive = true)))
    val config = Reg(Vec(connectionCount, new TcpConnectionOpen))
    val claimed = RegInit(VecInit(Seq.fill(connectionCount)(false.B)))
    val releaseListener = RegInit(VecInit(Seq.fill(connectionCount)(false.B)))
    val peerIp = Reg(Vec(connectionCount, UInt(32.W)))
    val peerPort = Reg(Vec(connectionCount, UInt(16.W)))
    // Count packets from dispatcher admission through complete validation.
    // In particular OPEN cannot overtake a prior generation's receive pipeline.
    val inFlight = RegInit(VecInit(Seq.fill(connectionCount)(0.U(3.W))))
    val admission = Wire(Vec(connectionCount, Bool()))
    val admitListener = Wire(Vec(connectionCount, Bool()))
    val inboundIp = Wire(UInt(32.W)); val inboundPort = Wire(UInt(16.W))

    val collect :: classify :: forward :: Nil = Enum(3)
    val receiveState = RegInit(collect)
    val header = Reg(Vec(3, new PacketBeat(16)))
    val headerCount = RegInit(0.U(2.W))
    val inputDone = RegInit(false.B)
    val routeValid = RegInit(false.B)
    val route = RegInit(0.U(connectionBits.W))
    val routeBusy = RegInit(false.B)
    val routeClosed = RegInit(false.B)
    val shared = Module(new SharedTcpReceive(packetBeats))
    shared.io.localIp := io.localIp
    val receiveTag = Reg(UInt(64.W))
    val closed = Module(new TcpResetTransmit(challengeTicks))
    closed.io.localIp := io.localIp; closed.io.netmask := io.netmask; closed.io.tick := io.tick
    io.exceptionOut <> closed.io.out
    io.exceptionDestinationIp := closed.io.destinationIp
    val ingress = Module(new MemoryQueue(new PacketBeat(16), 8))
    ingress.io.enq.valid := io.rx.valid && receiveState =/= classify && !inputDone
    ingress.io.enq.bits := io.rx.bits
    io.rx.ready := ingress.io.enq.ready && receiveState =/= classify && !inputDone
    when(io.rx.fire) {
        when(receiveState === collect) {
            header(headerCount) := io.rx.bits
            headerCount := headerCount + 1.U
            when(headerCount === 2.U || io.rx.bits.last) { receiveState := classify }
        }
        when(io.rx.bits.last) { inputDone := true.B }
    }
    def byte(i: Int): UInt = header(i / 16).data(8 * (i % 16) + 7, 8 * (i % 16))
    def field(i: Int, n: Int): UInt = Cat((i until i + n).map(byte))
    val sourceIp = field(12, 4); val destinationIp = field(16, 4)
    val sourcePort = field(20, 2); val destinationPort = field(22, 2)
    val flags = byte(33)
    val completeHeader = headerCount === 3.U && header(0).keep === "hffff".U &&
        header(1).keep === "hffff".U && header(2).keep(1, 0) === 3.U
    val supported = completeHeader && byte(0) === "h45".U && byte(9) === 6.U &&
        destinationIp === io.localIp
    def unicast(ip: UInt): Bool =
        ip(31, 28) < 14.U && ip(31, 24) =/= 0.U && ip(31, 24) =/= 127.U
    val syn = flags(1) && !flags(2) && !flags(4) &&
        unicast(sourceIp) && unicast(destinationIp)
    val tupleOwned = Wire(Vec(connectionCount, Bool()))
    val exact = Wire(Vec(connectionCount, Bool()))
    val listen = Wire(Vec(connectionCount, Bool()))
    val knownListener = Wire(Vec(connectionCount, Bool()))
    for (i <- 0 until connectionCount) {
        tupleOwned(i) := claimed(i) && config(i).localPort === destinationPort &&
            peerIp(i) === sourceIp && peerPort(i) === sourcePort
        exact(i) := tupleOwned(i) && !releaseListener(i) && cores(i).io.state =/= TcpState.closed.U
        knownListener(i) := cores(i).io.state === TcpState.listen.U &&
            config(i).localPort === destinationPort &&
            (config(i).remoteIp === 0.U || config(i).remoteIp === sourceIp) &&
            (config(i).remotePort === 0.U || config(i).remotePort === sourcePort)
        listen(i) := !claimed(i) && !releaseListener(i) && inFlight(i) === 0.U && cores(i).io.state === TcpState.listen.U &&
            config(i).localPort === destinationPort &&
            (config(i).remoteIp === 0.U || config(i).remoteIp === sourceIp) &&
            (config(i).remotePort === 0.U || config(i).remotePort === sourcePort)
    }
    val foundExact = exact.asUInt.orR
    val candidates = Mux(foundExact, exact.asUInt, listen.asUInt)
    val found = foundExact || (!tupleOwned.asUInt.orR && syn && listen.asUInt.orR)
    val chosen = PriorityEncoder(candidates).pad(connectionBits)
    def indexed[T <: Data](vector: Vec[T], index: UInt): T = if (connectionCount == 1) vector(0) else vector(index)
    val generations = VecInit(cores.map(_.io.generation))
    val states = VecInit(cores.map(_.io.state))
    // LISTEN admits one packet until its checksum/option validation resolves.
    val capacity = indexed(inFlight, chosen) < 4.U &&
        !(indexed(states, chosen) === TcpState.listen.U && indexed(inFlight, chosen) =/= 0.U)
    val classificationReady = !supported || !found || !capacity || shared.io.in.ready
    val accept = receiveState === classify && supported && found && capacity && shared.io.in.ready
    inboundIp := sourceIp; inboundPort := sourcePort
    for (i <- 0 until connectionCount) {
        admission(i) := accept && chosen === i.U
        admitListener(i) := admission(i) && !foundExact
        cores(i).io.rx.valid := false.B
        cores(i).io.rx.bits := 0.U.asTypeOf(new PacketBeat(16))
    }
    val resetCandidate = supported && !found && !tupleOwned.asUInt.orR &&
        (!knownListener.asUInt.orR || flags(4))
    when(receiveState === classify && classificationReady) {
        receiveState := forward; route := chosen
        routeValid := accept
        routeBusy := supported && found && !capacity
        routeClosed := resetCandidate && shared.io.in.ready
        // owned, exception, generation, slot (eight bits), zero-extended to 64.
        receiveTag := Cat(accept, resetCandidate && shared.io.in.ready,
            indexed(generations, chosen), chosen.pad(8))
    }
    shared.io.in.valid := receiveState === forward && (routeValid || routeClosed) && ingress.io.deq.valid
    shared.io.in.bits := ingress.io.deq.bits; shared.io.inContext := receiveTag
    ingress.io.deq.ready := receiveState === forward &&
        (!(routeValid || routeClosed) || shared.io.in.ready)
    val receiveDone = ingress.io.deq.fire && ingress.io.deq.bits.last
    val rawDrop = receiveDone && !routeValid
    when(receiveDone) {
        receiveState := collect; headerCount := 0.U; inputDone := false.B
        routeValid := false.B; routeBusy := false.B; routeClosed := false.B
    }

    def owned(context: UInt): Bool = context(41)
    def slot(context: UInt): UInt = context(connectionBits - 1, 0)
    def epoch(context: UInt): UInt = context(39, 8)
    val tag = shared.io.metadataContext
    val owner = slot(tag)
    val metadataReady = VecInit(cores.map(_.io.validated.get.metadata.ready))
    val epochMatches = epoch(tag) === indexed(generations, owner)
    val deliver = owned(tag) && epochMatches && indexed(metadataReady, owner)
    // Never park the common validator behind a stalled context. Offer metadata
    // only for the atomic transfer; rejected payload is drained to its own last.
    shared.io.metadata.ready := true.B
    val payloadOwner = Reg(UInt(connectionBits.W))
    val payloadDeliver = RegInit(false.B)
    when(shared.io.metadata.fire) { payloadOwner := owner; payloadDeliver := deliver }
    val payloadReady = VecInit(cores.map(_.io.validated.get.payload.ready))
    shared.io.payload.ready := !payloadDeliver || indexed(payloadReady, payloadOwner)
    for (i <- 0 until connectionCount) {
        val input = cores(i).io.validated.get
        input.metadata.valid := shared.io.metadata.valid && deliver && owner === i.U
        input.metadata.bits := shared.io.metadata.bits
        input.generation := epoch(tag)
        input.payload.valid := shared.io.payload.valid && payloadDeliver && payloadOwner === i.U
        input.payload.bits := shared.io.payload.bits
    }
    val metadataDropped = shared.io.metadata.fire && owned(tag) && !deliver
    val validationDrops = Wire(Vec(connectionCount, UInt(3.W)))
    for (i <- 0 until connectionCount) {
        validationDrops(i) := PopCount(VecInit(shared.io.drops.map(d =>
            d.valid && owned(d.bits) && slot(d.bits) === i.U) :+
            (metadataDropped && owner === i.U)))
    }
    io.rxDropped := rawDrop || metadataDropped
    io.rxBusyDropped := (rawDrop && routeBusy) || metadataDropped

    // Recheck ownership after shared validation, including a simultaneous OPEN.
    // An unowned packet cannot reset a tuple claimed while its checksum ran.
    val segment = shared.io.metadata.bits
    val responseOwned = (0 until connectionCount).map { i =>
        val tuple = config(i).localPort === segment.destinationPort &&
            peerIp(i) === segment.sourceIp && peerPort(i) === segment.sourcePort
        val listener = cores(i).io.state === TcpState.listen.U &&
            config(i).localPort === segment.destinationPort &&
            (config(i).remoteIp === 0.U || config(i).remoteIp === segment.sourceIp) &&
            (config(i).remotePort === 0.U || config(i).remotePort === segment.sourcePort)
        val opening = io.connections(i).open.fire &&
            io.connections(i).open.bits.localPort === segment.destinationPort &&
            (io.connections(i).open.bits.passive ||
                (io.connections(i).open.bits.remoteIp === segment.sourceIp &&
                 io.connections(i).open.bits.remotePort === segment.sourcePort))
        (claimed(i) && tuple) || (listener && !segment.flags(4)) ||
            (opening && (!io.connections(i).open.bits.passive || !segment.flags(4)))
    }.reduce(_ || _)
    closed.io.in.valid := shared.io.metadata.fire && tag(40) && !responseOwned
    closed.io.in.bits := segment

    val countFaults = Wire(Vec(connectionCount, Bool()))
    // A grant describes the preceding cycle's unchanged OPEN request. Any
    // accepted OPEN inserts one qualification bubble, so a simultaneous new
    // lower-priority request cannot use a grant based on the old claim table.
    val acceptedOpens = Wire(Vec(connectionCount, Bool()))
    val openBubble = RegNext(acceptedOpens.asUInt.orR, false.B)
    // Eligibility is independent of other OPEN grants. Comparing preceding
    // eligible requests gives the same lowest-slot winner for equal tuples,
    // without chaining each core's complete ready/fire path through all slots.
    val openExistingConflict = Wire(Vec(connectionCount, Bool()))
    val openEligible = Wire(Vec(connectionCount, Bool()))
    // Qualify availability one clock ahead for priority arbitration. Each
    // actual OPEN still handshakes against the core's current ready signal.
    val openAvailable = RegInit(VecInit(Seq.fill(connectionCount)(false.B)))
    for (i <- 0 until connectionCount) { openAvailable(i) := cores(i).io.open.ready }
    for (i <- 0 until connectionCount) {
        val app = io.connections(i)
        openExistingConflict(i) := (0 until connectionCount).filter(_ != i).map { j =>
            claimed(j) && config(j).localPort === app.open.bits.localPort &&
                peerIp(j) === app.open.bits.remoteIp && peerPort(j) === app.open.bits.remotePort
        }.foldLeft(false.B)(_ || _)
        openEligible(i) := app.open.valid && openAvailable(i) &&
            inFlight(i) === 0.U && receiveState =/= classify &&
            (app.open.bits.passive || !openExistingConflict(i))
    }
    for (i <- 0 until connectionCount) {
        val core = cores(i); val app = io.connections(i)
        core.io.localIp := io.localIp; core.io.tick := io.tick
        val existingConflict = openExistingConflict(i)
        val simultaneousConflict = (0 until i).map { j =>
            openEligible(j) && !io.connections(j).open.bits.passive &&
                io.connections(j).open.bits.localPort === app.open.bits.localPort &&
                io.connections(j).open.bits.remoteIp === app.open.bits.remoteIp &&
                io.connections(j).open.bits.remotePort === app.open.bits.remotePort
        }.foldLeft(false.B)(_ || _)
        val conflict = !app.open.bits.passive && (existingConflict || simultaneousConflict)
        val qualifyOpen = app.open.valid && inFlight(i) === 0.U &&
            receiveState =/= classify && !conflict && openAvailable(i)
        val grant = RegNext(qualifyOpen, false.B)
        val request = RegNext(app.open.bits.asUInt)
        val sameRequest = request === app.open.bits.asUInt
        // Listener admission only creates a claim in classify. Both sides of
        // this register boundary exclude classify; claim releases are safe.
        val canOpen = grant && sameRequest && !openBubble &&
            inFlight(i) === 0.U && receiveState =/= classify
        core.io.open.valid := app.open.valid && canOpen; core.io.open.bits := app.open.bits
        app.open.ready := core.io.open.ready && canOpen
        app.openConflict := app.open.valid && conflict && !app.open.ready
        acceptedOpens(i) := app.open.fire
        core.io.passiveInitialSequence := app.passiveInitialSequence
        core.io.close <> app.close; core.io.congestionWindow := app.congestionWindow
        core.io.netmask := io.netmask; core.io.pathMtuLimit := io.pathMtuLimit
        core.io.icmpError := io.icmpError; core.io.exceptionOut.ready := true.B
        app.pathMtu := core.io.pathMtu; app.networkError := core.io.networkError
        app.notice <> core.io.notice; core.io.allocate <> app.allocate
        core.io.in <> app.in; app.stored <> core.io.stored; app.received <> core.io.received
        app.congestionLimit := core.io.congestionLimit; app.congestionThreshold := core.io.congestionThreshold
        app.fastRecovery := core.io.fastRecovery; app.timeout <> core.io.timeout
        app.state := core.io.state; app.generation := core.io.generation
        app.sendOldest := core.io.sendOldest; app.sendNext := core.io.sendNext
        app.sendActual := core.io.sendActual; app.receiveNext := core.io.receiveNext
        app.sendMss := core.io.sendMss; app.receiveWindow := core.io.receiveWindow
        app.freeBanks := core.io.freeBanks; app.outstanding := core.io.outstanding
        app.timerRunning := core.io.timerRunning; app.timerExpired := core.io.timerExpired
        app.rto := core.io.rto; app.retries := core.io.retries
        app.reorderCount := core.io.reorderCount; app.reorderDropped := core.io.reorderDropped
        app.completionAccepted := core.io.completionAccepted

        val retired = core.io.rxConsumed.asUInt +& validationDrops(i) +& core.io.rxDrops
        val available = inFlight(i) +& admission(i).asUInt
        countFaults(i) := retired > available
        when(admission(i) || retired =/= 0.U) { inFlight(i) := available - retired }
        val previousState = RegNext(core.io.state, TcpState.closed.U)
        when(core.io.state === TcpState.listen.U && previousState =/= TcpState.listen.U &&
            previousState =/= TcpState.closed.U) { releaseListener(i) := true.B }
        when(releaseListener(i) && inFlight(i) === 0.U && core.io.ownershipDrained) {
            when(core.io.state === TcpState.listen.U) { claimed(i) := false.B }
            releaseListener(i) := false.B
        }
        // Keep the four-tuple reserved while an abort's old frame or completion
        // is still owned by the MAC, even though the TCB already says CLOSED.
        when(core.io.state === TcpState.closed.U) {
            when(core.io.open.ready && inFlight(i) === 0.U) { claimed(i) := false.B }
            releaseListener(i) := false.B
        }
        when((validationDrops(i) =/= 0.U || core.io.rxDrops =/= 0.U) && core.io.state === TcpState.listen.U &&
            retired === available && core.io.ownershipDrained) { claimed(i) := false.B }
        when(admitListener(i)) {
            claimed(i) := true.B; peerIp(i) := inboundIp; peerPort(i) := inboundPort
        }
        when(app.open.fire) {
            config(i) := app.open.bits
            config(i).localIp := io.localIp
            claimed(i) := !app.open.bits.passive
            peerIp(i) := app.open.bits.remoteIp; peerPort(i) := app.open.bits.remotePort
        }
    }

    class CompletionOwner extends Bundle {
        val connection = UInt(connectionBits.W)
        val globalTag = UInt(32.W); val localTag = UInt(32.W); val generation = UInt(32.W)
    }
    val ledger = Module(new MemoryQueue(new CompletionOwner, globalDepth))
    val nextTag = RegInit(0.U(32.W))
    val nextConnection = RegInit(0.U(connectionBits.W))
    val locked = RegInit(false.B); val selected = RegInit(0.U(connectionBits.W))
    // Non-flowing two-beat queues cut the shared ready path between contexts.
    // Carry the complete descriptor with every beat: the producer may finish
    // this packet and advance its local tag before the shared arbiter drains it.
    val transmit = cores.map { core =>
        val queue = Module(new MemoryQueue(new Bundle {
            val beat = new PacketBeat(16)
            val packet = new TcpWireDescriptor
        }, 2))
        queue.io.enq.valid := core.io.out.valid
        queue.io.enq.bits.beat := core.io.out.bits
        queue.io.enq.bits.packet := core.io.packet
        core.io.out.ready := queue.io.enq.ready
        queue
    }
    val requests = VecInit(transmit.map(_.io.deq.valid))
    val order = (0 until connectionCount).map { offset =>
        val sum = nextConnection +& offset.U
        Mux(sum >= connectionCount.U, sum - connectionCount.U, sum)(connectionBits - 1, 0)
    }
    val candidate = PriorityMux(order.map(index => indexed(requests, index) -> index))
    val anyRequest = requests.asUInt.orR
    val txOwner = Mux(locked, selected, candidate)
    val grant = (locked || anyRequest) && (locked || ledger.io.enq.ready)
    val streams = VecInit(transmit.map(_.io.deq.bits.beat))
    val descriptors = VecInit(transmit.map(_.io.deq.bits.packet))
    io.out.valid := grant && indexed(requests, txOwner)
    io.out.bits := indexed(streams, txOwner)
    io.packet := indexed(descriptors, txOwner); io.packet.tag := nextTag
    io.packetConnection := txOwner
    for (i <- 0 until connectionCount) { transmit(i).io.deq.ready := io.out.ready && grant && txOwner === i.U }
    when(io.out.valid && !locked) { locked := true.B; selected := txOwner }
    val submitted = io.out.fire && io.out.bits.last
    when(submitted) {
        locked := false.B; nextTag := nextTag + 1.U
        nextConnection := Mux(txOwner === (connectionCount - 1).U, 0.U, txOwner + 1.U)
    }
    ledger.io.enq.valid := submitted
    ledger.io.enq.bits.connection := txOwner; ledger.io.enq.bits.globalTag := nextTag
    ledger.io.enq.bits.localTag := indexed(descriptors, txOwner).tag
    ledger.io.enq.bits.generation := indexed(descriptors, txOwner).completion.generation
    val head = ledger.io.deq.bits
    val matches = ledger.io.deq.valid && io.completed.bits.tag === head.globalTag &&
        io.completed.bits.generation === head.generation
    for (i <- 0 until connectionCount) {
        cores(i).io.completed.valid := io.completed.valid && matches && head.connection === i.U
        cores(i).io.completed.bits.tag := head.localTag
        cores(i).io.completed.bits.generation := head.generation
        cores(i).io.completed.bits.failed := io.completed.bits.failed
    }
    val accepted = VecInit(cores.map(_.io.completionAccepted))
    val ownerAccepted = (0 until connectionCount).map(i => accepted(i) && head.connection === i.U).reduce(_ || _)
    ledger.io.deq.ready := io.completed.valid && matches && ownerAccepted
    io.completionAccepted := ledger.io.deq.fire
    io.invalidCompletion := io.completed.valid && !io.completionAccepted
    io.outstanding := ledger.io.count
    io.rxDrops := cores.map(_.io.rxDrops).reduce(_ +& _) +&
        PopCount(VecInit(shared.io.drops.map(d => d.valid && owned(d.bits)) ++ Seq(rawDrop, metadataDropped)))
    io.fault := cores.map(_.io.fault).reduce(_ || _) || countFaults.asUInt.orR ||
        (ledger.io.enq.valid && !ledger.io.enq.ready) ||
        (io.completed.valid && matches && !ownerAccepted)
}
