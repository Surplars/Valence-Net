package netstack

import chisel3._
import chisel3.util._

class TcpPayloadSegment extends Bundle {
    val generation = UInt(32.W)
    val sequence = UInt(32.W)
    val length = UInt(16.W)
    val context = UInt(64.W)
    val retransmission = Bool()
    val push = Bool()
}

/** One connection's retained TX data and MSS/credit-limited segment scheduler.
  *
  * sendOldest/sendActual/sendSequence/credit must come from the same validated
  * TCB. Credit includes both peer window and congestion window, minus already
  * reserved flight. reserve is an atomic allocation request (Decoupled, not
  * Irrevocable); it can change as credit changes or a replay takes priority.
  * No sequence is allocated for an incomplete or malformed application write.
  * Reads pin the payload before a segment descriptor becomes externally visible.
  * Thus a cumulative ACK may reclaim the descriptor while a stalled replay
  * continues reading its original bytes safely.
  *
  * A reset/connection abort must flush this queue AND downstream packet queues
  * together, after cancelling/draining the corresponding wire descriptors.
  * Merely changing generation is not a flush. App writes are bounded by one
  * bank; larger application streams are supplied as consecutive write blocks.
  */
class TcpTransmitQueue(val banks: Int = 8, val beatsPerBank: Int = 128) extends Module {
    require(isPow2(banks) && banks >= 2 && banks <= 256)
    val bits = log2Ceil(banks)
    val io = IO(new Bundle {
        val allocate = Flipped(Decoupled(new TcpStoreAllocation))
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val stored = Decoupled(new TcpStoreResult)
        val enabled = Input(Bool())
        val generation = Input(UInt(32.W))
        val sendOldest = Input(UInt(32.W))
        val sendActual = Input(UInt(32.W))
        val sendSequence = Input(UInt(32.W))
        val credit = Input(UInt(32.W))
        val mss = Input(UInt(16.W))
        val reserve = Decoupled(UInt(16.W))
        val replay = Flipped(Decoupled(UInt(32.W)))
        val replayLimit = Input(UInt(16.W))
        val replayMiss = Output(Bool())
        val segment = Decoupled(new TcpPayloadSegment)
        val out = Decoupled(new PacketBeat(16))
        val freeBanks = Output(UInt(log2Ceil(banks + 1).W))
        val fault = Output(Bool())
        val unsent = Output(Bool())
        val pendingPackets = Output(Bool())
    })
    // All three comparisons start together, rather than putting a second
    // subtract/compare behind a selected minimum.
    def min3(a: UInt, b: UInt, c: UInt): UInt =
        Mux(a <= b && a <= c, a, Mux(b <= c, b, c))
    // Keep every replay bound comparison parallel; a selected MSS minimum
    // would otherwise feed a second comparison before issuing the RAM read.
    def min4(a: UInt, b: UInt, c: UInt, d: UInt): UInt =
        Mux(a <= b && a <= c && a <= d, a,
            Mux(b <= c && b <= d, b, Mux(c <= d, c, d)))
    def ge(a: UInt, b: UInt): Bool = !(a - b)(31)
    val store = Module(new TcpPayloadStore(banks, beatsPerBank))
    store.io.allocate <> io.allocate
    store.io.in <> io.in
    io.freeBanks := store.io.freeBanks

    val live = RegInit(VecInit(Seq.fill(banks)(false.B)))
    val assigned = RegInit(VecInit(Seq.fill(banks)(false.B)))
    val tokens = Reg(Vec(banks, UInt(32.W)))
    val lengths = Reg(Vec(banks, UInt(16.W)))
    val scheduled = RegInit(VecInit(Seq.fill(banks)(0.U(16.W))))
    val bases = Reg(Vec(banks, UInt(32.W)))
    val contexts = Reg(Vec(banks, UInt(64.W)))
    val order = Module(new Queue(UInt(bits.W), banks))
    val writing = RegInit(false.B)
    when(io.allocate.fire) { writing := true.B }
    when(io.stored.fire) { writing := false.B }
    io.unsent := writing || order.io.deq.valid
    val result = store.io.stored.bits
    val enqueue = !result.error && result.length =/= 0.U
    val resultRoom = !enqueue || order.io.enq.ready
    io.stored.valid := store.io.stored.valid && resultRoom
    io.stored.bits := result
    store.io.stored.ready := io.stored.ready && resultRoom
    order.io.enq.valid := store.io.stored.valid && io.stored.ready && enqueue
    order.io.enq.bits := result.token(bits - 1, 0)
    when(store.io.stored.fire && !result.error) {
        val i = result.token(bits - 1, 0)
        live(i) := true.B; assigned(i) := false.B
        tokens(i) := result.token; lengths(i) := result.length
        contexts(i) := result.context; scheduled(i) := 0.U
    }

    val reading = RegInit(false.B)
    when(store.io.read.fire) { reading := true.B }
    when(io.out.fire && io.out.bits.last) { reading := false.B }
    io.pendingPackets := reading
    val pending = RegInit(false.B)
    val segment = Reg(new TcpPayloadSegment)
    io.segment.valid := pending && store.io.response.valid && !store.io.response.bits.error
    io.segment.bits := segment
    store.io.response.ready := pending && (io.segment.ready || store.io.response.bits.error)
    when(store.io.response.fire) { pending := false.B }
    io.out <> store.io.out

    val head = order.io.deq.bits
    val remaining = lengths(head) - scheduled(head)
    val newLength = min3(remaining, io.mss, io.credit)(15, 0)
    val newCandidate = io.enabled && order.io.deq.valid && remaining =/= 0.U &&
        io.mss =/= 0.U && io.credit =/= 0.U

    // Replay is a cold control path. Resolve bank ownership across two
    // register boundaries before issuing a RAM read; new data keeps its
    // original scheduling cadence. Sampling before ready is speculative.
    // The requester may withdraw/change its offer: that cancels the lookup.
    val lookup = RegInit(0.U(2.W))
    val replaySequence = Reg(UInt(32.W))
    val replayHits = Reg(Vec(banks, Bool()))
    val replayOffsets = Reg(Vec(banks, UInt(16.W)))
    val replayRemaining = Reg(Vec(banks, UInt(16.W)))
    val replayBank = Reg(UInt(bits.W))
    val replayOffset = Reg(UInt(16.W))
    val replayAvailable = Reg(UInt(16.W))
    val replayFound = RegInit(false.B)
    val replayToken = Reg(UInt(32.W))
    val replayContext = Reg(UInt(64.W))
    val replayBlockLength = Reg(UInt(16.W))
    val sameRequest = io.replay.valid && io.replay.bits === replaySequence
    when(lookup === 0.U && io.replay.valid) {
        lookup := 1.U
        replaySequence := io.replay.bits
        for (i <- 0 until banks) {
            val delta = io.replay.bits - bases(i)
            replayHits(i) := live(i) && assigned(i) && delta < scheduled(i)
            replayOffsets(i) := delta(15, 0)
            replayRemaining(i) := scheduled(i) - delta(15, 0)
        }
    }
    when(lookup === 1.U && sameRequest) {
        val chosen = PriorityEncoder(replayHits.asUInt)
        lookup := 2.U
        replayBank := chosen
        replayFound := replayHits.asUInt.orR
        replayOffset := replayOffsets(chosen)
        replayAvailable := replayRemaining(chosen)
        replayToken := tokens(chosen)
        replayContext := contexts(chosen)
        replayBlockLength := lengths(chosen)
    }
    when(lookup =/= 0.U && !sameRequest) { lookup := 0.U }

    // ACK/actual/MSS can change during lookup. Revalidate their current values
    // at the request handshake, without feeding them back into bank selection.
    val wireFlight = io.sendActual - io.sendOldest
    val replayInFlight = (replaySequence - io.sendOldest) < wireFlight
    val replayLength = min4(replayAvailable, io.replayLimit, io.mss,
        io.sendActual - replaySequence)(15, 0)
    // min(limit, MSS) is nonzero exactly when both unsigned bounds are nonzero.
    // Validity must not wait for the selected minimum used by length arithmetic.
    val replayGood = replayFound && replayInFlight && io.replayLimit =/= 0.U && io.mss =/= 0.U
    val replayResolved = lookup === 2.U && sameRequest
    io.replay.ready := !pending && store.io.read.ready && replayResolved
    io.replayMiss := io.replay.fire && !replayGood
    when(io.replay.fire) { lookup := 0.U }
    io.reserve.valid := newCandidate && !pending && store.io.read.ready &&
        !io.replay.valid && lookup === 0.U
    io.reserve.bits := newLength
    val retry = io.replay.valid
    val length = Mux(retry, replayLength, newLength)
    val offset = Mux(retry, replayOffset, scheduled(head))
    store.io.read.valid := !pending && Mux(retry, replayResolved && replayGood,
        lookup === 0.U && newCandidate && io.reserve.ready)
    store.io.read.bits.token := Mux(retry, replayToken, tokens(head))
    store.io.read.bits.offset := offset
    store.io.read.bits.length := length
    order.io.deq.ready := io.reserve.fire && newLength === remaining

    when(store.io.read.fire) {
        pending := true.B
        segment.generation := io.generation
        segment.sequence := Mux(retry, replaySequence, io.sendSequence)
        segment.length := length
        segment.context := Mux(retry, replayContext, contexts(head))
        segment.retransmission := retry
        // Compare against an independently computed tail rather than adding
        // the late MSS/credit-selected length before testing the bank end.
        // Seventeen bits preserve exact equality even for invalid offsets.
        val newTail = lengths(head).pad(17) - scheduled(head).pad(17)
        val replayTail = replayBlockLength.pad(17) - replayOffset.pad(17)
        segment.push := Mux(retry, replayLength.pad(17) === replayTail,
            newLength.pad(17) === newTail)
    }
    when(io.reserve.fire) {
        when(!assigned(head)) { bases(head) := io.sendSequence; assigned(head) := true.B }
        scheduled(head) := scheduled(head) + newLength
    }

    val reclaim = VecInit((0 until banks).map { i =>
        // The first lookup cycle protects all potential candidates. Later
        // stages pin only their registered candidates, so other ACKed banks
        // can still be reused while a replay is blocked downstream. Once a
        // read is accepted, TcpPayloadStore owns the pin until its last beat.
        val lookupPin = (lookup === 0.U && io.replay.valid) ||
            (lookup === 1.U && replayHits(i)) ||
            (lookup === 2.U && replayFound && replayBank === i.U)
        // A new-data read always targets a not-fully-scheduled bank, which
        // cannot satisfy this reclamation predicate. No read.fire feedback
        // is needed through priority selection/token comparison/release.
        live(i) && !lookupPin && (lengths(i) === 0.U ||
            (assigned(i) && scheduled(i) === lengths(i) &&
                ge(io.sendOldest, bases(i) + lengths(i))))
    })
    val retired = PriorityEncoder(reclaim.asUInt)
    store.io.release.valid := reclaim.asUInt.orR
    store.io.release.bits := tokens(retired)
    when(store.io.release.valid) { live(retired) := false.B }
    // Diagnostic only: isolate reclamation/token checks from parent fault ORs.
    io.fault := RegNext(store.io.releaseMiss || (store.io.response.fire && store.io.response.bits.error), false.B)
}

class TcpDataHeader extends Bundle {
    val sourceIp = UInt(32.W); val destinationIp = UInt(32.W)
    val sourcePort = UInt(16.W); val destinationPort = UInt(16.W)
    val acknowledgement = UInt(32.W); val window = UInt(16.W)
    val ttl = UInt(8.W); val tos = UInt(8.W)
    val optionLength = UInt(6.W); val options = UInt(320.W)
}

/** Retention -> segmentation/replay -> checksummed IPv4/TCP packets.
  * packet identifies the descriptor for EVERY offered output beat. Completion
  * at this IP boundary is not physical transmission: carry this descriptor
  * through routing/MAC, and report its end sequence to the TCB only on wire
  * completion. The next packet may be prepared while the previous is stalled.
  * header.ttl must be nonzero; options are preencoded by the owning TCB.
  */
class TcpDataTransmit(val banks: Int = 8, val beatsPerBank: Int = 128) extends Module {
    val io = IO(new Bundle {
        val allocate = Flipped(Decoupled(new TcpStoreAllocation))
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val stored = Decoupled(new TcpStoreResult)
        val enabled = Input(Bool()); val generation = Input(UInt(32.W))
        val sendOldest = Input(UInt(32.W)); val sendActual = Input(UInt(32.W))
        val sendSequence = Input(UInt(32.W)); val credit = Input(UInt(32.W))
        val mss = Input(UInt(16.W)); val reserve = Decoupled(UInt(16.W))
        val replay = Flipped(Decoupled(UInt(32.W)))
        val replayLimit = Input(UInt(16.W))
        val replayMiss = Output(Bool())
        val header = Input(new TcpDataHeader)
        val out = Decoupled(new PacketBeat(16))
        val packet = Output(new TcpPayloadSegment)
        val prepared = Valid(new TcpPayloadSegment)
        val freeBanks = Output(UInt(log2Ceil(banks + 1).W))
        val fault = Output(Bool())
        val unsent = Output(Bool())
        val pendingPackets = Output(Bool())
    })
    val data = Module(new TcpTransmitQueue(banks, beatsPerBank))
    val serializer = Module(new Ipv4TcpTransmit(beatsPerBank))
    val descriptors = Module(new Queue(new TcpPayloadSegment, 4))
    val identification = RegInit(0.U(16.W))
    data.io.allocate <> io.allocate; data.io.in <> io.in; io.stored <> data.io.stored
    data.io.enabled := io.enabled; data.io.generation := io.generation
    data.io.sendOldest := io.sendOldest; data.io.sendActual := io.sendActual
    data.io.sendSequence := io.sendSequence; data.io.credit := io.credit; data.io.mss := io.mss
    io.reserve <> data.io.reserve; data.io.replay <> io.replay
    data.io.replayLimit := io.replayLimit
    io.replayMiss := data.io.replayMiss; io.freeBanks := data.io.freeBanks
    data.io.segment.ready := serializer.io.command.ready && descriptors.io.enq.ready
    serializer.io.command.valid := data.io.segment.valid && descriptors.io.enq.ready
    descriptors.io.enq.valid := data.io.segment.valid && serializer.io.command.ready
    descriptors.io.enq.bits := data.io.segment.bits
    // Match the exact header snapshot used to budget this segment's MSS.
    // A new RX SACK/window update while RAM/serializer is stalled cannot grow it.
    val header = Reg(new TcpDataHeader)
    when(data.io.reserve.fire || (data.io.replay.fire && !data.io.replayMiss)) { header := io.header }
    val cmd = serializer.io.command.bits
    cmd := 0.U.asTypeOf(new TcpSendCommand)
    cmd.sourceIp := header.sourceIp; cmd.destinationIp := header.destinationIp
    cmd.sourcePort := header.sourcePort; cmd.destinationPort := header.destinationPort
    cmd.acknowledgement := header.acknowledgement; cmd.window := header.window
    cmd.ttl := header.ttl; cmd.tos := header.tos
    cmd.optionLength := header.optionLength; cmd.options := header.options
    cmd.sequence := data.io.segment.bits.sequence
    cmd.flags := Mux(data.io.segment.bits.push, 24.U, 16.U)
    cmd.length := data.io.segment.bits.length
    cmd.identification := identification
    when(serializer.io.command.fire) { identification := identification + 1.U }
    serializer.io.in <> data.io.out
    io.out <> serializer.io.out
    io.packet := descriptors.io.deq.bits
    io.unsent := data.io.unsent
    io.pendingPackets := data.io.pendingPackets || descriptors.io.deq.valid
    descriptors.io.deq.ready := io.out.fire && io.out.bits.last
    io.prepared.valid := data.io.segment.fire; io.prepared.bits := data.io.segment.bits
    io.fault := data.io.fault || serializer.io.dropped || (io.out.valid && !descriptors.io.deq.valid)
}
