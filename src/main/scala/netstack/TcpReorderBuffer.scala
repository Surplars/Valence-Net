package netstack

import chisel3._
import chisel3.util._

class TcpReorderEntry extends Bundle {
    val generation = UInt(32.W)
    val sequence = UInt(32.W)
    val length = UInt(16.W)
    val fin = Bool()
}

/** Bounded receive-side retention. Only the TCB admits validated future data.
  * Overlapping cached ranges are dropped as a whole; cumulative ACK recovery
  * remains authoritative. FIN occupies sequence space but no payload byte.
  * Separate storage preserves the receive ring's previously advertised edge.
  */
class TcpReorderBuffer(val banks: Int = 8, val beats: Int = 128) extends Module {
    val io = IO(new Bundle {
        val enqueue = Flipped(Decoupled(new TcpReorderEntry))
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val replay = Decoupled(new TcpReorderEntry)
        val out = Decoupled(new PacketBeat(16))
        val receiveNext = Input(UInt(32.W)); val generation = Input(UInt(32.W))
        val credit = Input(UInt(32.W)); val enabled = Input(Bool())
        val clear = Input(Bool()); val select = Input(Bool())
        val pending = Output(Bool()); val empty = Output(Bool())
        val count = Output(UInt(log2Ceil(banks + 1).W)); val fault = Output(Bool())
        val sack = Output(new TcpSackBlocks); val sackUpdated = Output(Bool()); val sackBusy = Output(Bool())
        val sackStateBusy = Output(Bool()) // excludes this cycle's admission/touch
    })
    def after(a: UInt, b: UInt): Bool = { val d = (a - b)(31, 0); d =/= 0.U && !d(31) }
    def end(e: TcpReorderEntry): UInt = (e.sequence + e.length + e.fin)(31, 0)
    val store = Module(new TcpPayloadStore(banks, beats))
    val descriptors = Reg(Vec(banks, new TcpReorderEntry))
    val ends = Reg(Vec(banks, UInt(32.W)))
    val tokens = Reg(Vec(banks, UInt(32.W)))
    val owned = RegInit(VecInit(Seq.fill(banks)(false.B)))
    val valid = RegInit(VecInit(Seq.fill(banks)(false.B)))
    val writing = RegInit(false.B)
    val writeSlot = Reg(UInt(log2Ceil(banks).W))
    val cancelled = RegInit(false.B)
    val age = RegInit(VecInit(Seq.fill(banks)(banks.U(log2Ceil(banks + 1).W))))
    val sack = Module(new TcpSackSnapshot(banks))
    sack.io.base := io.receiveNext; sack.io.clear := io.clear || !io.enabled
    val touched = WireDefault(false.B)
    sack.io.changed := RegNext((store.io.stored.fire && !store.io.stored.bits.error && !cancelled) || touched, false.B)
    when(store.io.stored.fire && !store.io.stored.bits.error && !cancelled) {
        for (i <- 0 until banks) { when(valid(i) && age(i) < (banks - 1).U) { age(i) := age(i) + 1.U } }
        age(writeSlot) := 0.U
    }
    for (i <- 0 until banks) {
        sack.io.valid(i) := valid(i) && descriptors(i).generation === io.generation &&
            descriptors(i).length =/= 0.U && after(descriptors(i).sequence, io.receiveNext)
        sack.io.left(i) := descriptors(i).sequence
        sack.io.right(i) := descriptors(i).sequence + descriptors(i).length
        sack.io.age(i) := age(i)
    }
    io.sack := sack.io.blocks; io.sackUpdated := sack.io.updated
    io.sackStateBusy := sack.io.busy || writing
    io.sackBusy := io.sackStateBusy || touched || io.enqueue.fire
    val idle :: offer :: request :: response :: data :: failed :: Nil = Enum(6)
    val state = RegInit(idle)
    val readSlot = Reg(UInt(log2Ceil(banks).W))
    val entry = Reg(new TcpReorderEntry)
    val readToken = Reg(UInt(32.W))
    val readEnd = Reg(UInt(32.W))

    val stale = Wire(Vec(banks, Bool()))
    val eligible = Wire(Vec(banks, Bool()))
    val overlap = Wire(Vec(banks, Bool()))
    val free = Wire(Vec(banks, Bool()))
    val garbage = Wire(Vec(banks, Bool()))
    val incomingEnd = end(io.enqueue.bits)
    for (i <- 0 until banks) {
        stale(i) := !io.enabled || descriptors(i).generation =/= io.generation ||
            !after(ends(i), io.receiveNext)
        eligible(i) := valid(i) && !stale(i) &&
            !after(descriptors(i).sequence, io.receiveNext) &&
            (ends(i) - io.receiveNext)(31, 0) <= io.credit
        overlap(i) := valid(i) && !stale(i) &&
            after(incomingEnd, descriptors(i).sequence) &&
            after(ends(i), io.enqueue.bits.sequence)
        free(i) := !owned(i) && !(writing && writeSlot === i.U)
        garbage(i) := owned(i) && !valid(i) && !(state =/= idle && readSlot === i.U)
        when(io.clear || (valid(i) && stale(i))) { valid(i) := false.B }
    }
    // A duplicate wholly inside a retained payload refreshes its SACK recency.
    // No new payload ownership is needed, and unretained overlap is not claimed.
    val repeated = VecInit((0 until banks).map(i => valid(i) && !stale(i) &&
        io.enqueue.bits.length =/= 0.U && !after(descriptors(i).sequence, io.enqueue.bits.sequence) &&
        !after(io.enqueue.bits.sequence + io.enqueue.bits.length, descriptors(i).sequence + descriptors(i).length)))
    touched := io.enqueue.valid && overlap.asUInt.orR && repeated.asUInt.orR && !io.clear
    when(touched) {
        val target = PriorityEncoder(repeated.asUInt)
        for (i <- 0 until banks) {
            when(valid(i) && age(i) < age(target)) { age(i) := age(i) + 1.U }
        }
        age(target) := 0.U
    }
    val chosen = PriorityEncoder(free.asUInt)
    val accept = !writing && free.asUInt.orR && !overlap.asUInt.orR &&
        io.enqueue.bits.length <= (beats * 16).U &&
        io.enqueue.bits.generation === io.generation && io.enabled && !io.clear
    store.io.allocate.valid := io.enqueue.valid && accept
    store.io.allocate.bits.length := io.enqueue.bits.length
    store.io.allocate.bits.context := 0.U
    io.enqueue.ready := accept && store.io.allocate.ready
    when(io.enqueue.fire) {
        writing := true.B; writeSlot := chosen; cancelled := false.B
        descriptors(chosen) := io.enqueue.bits
        ends(chosen) := incomingEnd
    }
    store.io.in.valid := io.in.valid && writing
    store.io.in.bits := io.in.bits
    io.in.ready := writing && store.io.in.ready
    store.io.stored.ready := writing
    when(store.io.stored.fire) {
        writing := false.B
        tokens(writeSlot) := store.io.stored.bits.token
        owned(writeSlot) := !store.io.stored.bits.error
        valid(writeSlot) := !store.io.stored.bits.error && !cancelled && !io.clear &&
            io.enabled && descriptors(writeSlot).generation === io.generation
    }
    when(io.clear) { cancelled := true.B }

    val candidate = PriorityEncoder(eligible.asUInt)
    io.pending := (eligible.asUInt.orR || state =/= idle) && !io.clear
    when(state === idle && io.select && eligible.asUInt.orR && !io.clear) {
        readSlot := candidate; entry := descriptors(candidate)
        readToken := tokens(candidate); readEnd := ends(candidate); state := offer
    }
    io.replay.valid := state === offer && !io.clear && io.enabled &&
        entry.generation === io.generation
    io.replay.bits := entry
    when(state === offer && (io.clear || !io.enabled || entry.generation =/= io.generation)) {
        state := idle
    }
    when(io.replay.fire) { state := request }
    store.io.read.valid := state === request
    store.io.read.bits.token := readToken
    store.io.read.bits.offset := 0.U
    store.io.read.bits.length := entry.length
    when(store.io.read.fire) { state := response }
    store.io.response.ready := state === response
    when(store.io.response.fire) {
        state := Mux(store.io.response.bits.error, failed, data)
    }
    io.out.valid := (state === data && store.io.out.valid) || state === failed
    io.out.bits := store.io.out.bits
    when(state === failed) {
        io.out.bits := 0.U.asTypeOf(new PacketBeat(16))
        io.out.bits.last := true.B; io.out.bits.error := true.B
    }
    store.io.out.ready := state === data && io.out.ready
    val finished = io.out.fire && io.out.bits.last
    val retireRead = finished && (io.clear || !io.enabled ||
        entry.generation =/= io.generation || !after(readEnd, io.receiveNext) || state === failed)
    when(finished) { state := idle }
    val garbageSlot = PriorityEncoder(garbage.asUInt)
    store.io.release.valid := retireRead || garbage.asUInt.orR
    store.io.release.bits := Mux(retireRead, readToken, tokens(garbageSlot))
    when(retireRead) { owned(readSlot) := false.B; valid(readSlot) := false.B }
    .elsewhen(garbage.asUInt.orR) { owned(garbageSlot) := false.B }
    io.count := PopCount(valid)
    io.empty := !owned.asUInt.orR && !writing && state === idle
    io.fault := store.io.releaseMiss || (store.io.stored.fire && store.io.stored.bits.error) ||
        (store.io.response.fire && store.io.response.bits.error)
}
