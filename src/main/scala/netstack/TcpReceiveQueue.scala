package netstack

import chisel3._
import chisel3.util._

/** Serialized receive commit/copy, with a separate bounded reorder store.
  * Wire metadata has passed full packet validation. Replay re-enters only the
  * receive half of the TCB, never its ACK, RTT or congestion-control processing.
  */
class TcpReceiveQueue(val words: Int = 4096) extends Module {
    val io = IO(new Bundle {
        val metadata = Flipped(Decoupled(new TcpSegment))
        val payload = Flipped(Decoupled(new PacketBeat(16)))
        val rx = Decoupled(new TcpConnectionRx)
        val disposition = Flipped(Decoupled(new TcpRxDisposition))
        val out = Decoupled(new PacketBeat(16))
        val window = Output(UInt(32.W))
        val updateWindow = Output(Bool())
        val sack = Output(new TcpSackBlocks)
        val receiveNext = Input(UInt(32.W)); val generation = Input(UInt(32.W))
        val credit = Input(UInt(32.W)); val enabled = Input(Bool())
        val deliver = Input(Bool())
        val clear = Input(Bool())
        val empty = Output(Bool())
        val busy = Output(Bool())
        val fault = Output(Bool())
        val reorderCount = Output(UInt(4.W)); val reorderDropped = Output(Bool())
    })
    val ring = withReset(reset.asBool || io.clear) { Module(new TcpReceiveRing(words)) }
    val slice = Module(new PacketSlice)
    val reorder = Module(new TcpReorderBuffer)
    val idle :: decision :: copy :: Nil = Enum(3)
    val state = RegInit(idle)
    val token = RegInit(0.U(32.W))
    val cancelled = RegInit(false.B)
    val keep = RegInit(false.B)
    val caching = RegInit(false.B)
    val replaying = RegInit(false.B)
    val sequence = Reg(UInt(32.W))
    val expectedToken = Reg(UInt(32.W))
    reorder.io.receiveNext := io.receiveNext; reorder.io.generation := io.generation
    // Selection happens only after the previous serialized copy has drained.
    // Credit has then caught up with its reservation; while an offer waits,
    // application reads can only increase free space. Keep scaling out of
    // the reassembly-choice -> wire-metadata-ready combinational path.
    val reorderCredit = RegNext(io.credit, 0.U(32.W))
    reorder.io.credit := reorderCredit; reorder.io.enabled := io.enabled
    reorder.io.clear := io.clear; reorder.io.select := state === idle
    val chooseReplay = reorder.io.pending
    // Only future-data metadata waits for the previous cold-path SACK snapshot.
    // In-order traffic and cumulative ACK processing keep their normal cadence.
    // Metadata can transfer only in idle. Enqueue/touch require decision, so
    // they are false whenever this guard is observed. Use registered writer/
    // snapshot work directly, cutting admission arithmetic out of metadata ready.
    val sackWait = reorder.io.sackStateBusy && io.metadata.bits.payloadLength =/= 0.U &&
        TcpSackEncoding.after(io.metadata.bits.sequence, io.receiveNext)
    io.rx.valid := state === idle && Mux(chooseReplay, reorder.io.replay.valid, io.metadata.valid && !sackWait) && !io.clear
    io.rx.bits.segment := io.metadata.bits
    when(chooseReplay) {
        io.rx.bits.segment := 0.U.asTypeOf(new TcpSegment)
        io.rx.bits.segment.sequence := reorder.io.replay.bits.sequence
        io.rx.bits.segment.payloadLength := reorder.io.replay.bits.length
        io.rx.bits.segment.flags := Mux(reorder.io.replay.bits.fin, 1.U, 0.U)
    }
    io.rx.bits.payloadToken := token
    io.rx.bits.payloadStored := true.B
    io.rx.bits.reassembled := chooseReplay
    io.rx.bits.replayGeneration := reorder.io.replay.bits.generation
    io.metadata.ready := state === idle && io.rx.ready && !io.clear && !chooseReplay && !sackWait
    reorder.io.replay.ready := state === idle && io.rx.ready && !io.clear && chooseReplay
    when(io.rx.fire) {
        state := decision; expectedToken := token; token := token + 1.U; cancelled := false.B
        replaying := chooseReplay; sequence := io.rx.bits.segment.sequence
    }
    val active = !cancelled && !io.clear
    val future = io.disposition.bits.outOfOrder && !replaying && active
    val retain = io.disposition.bits.keep && io.disposition.bits.length =/= 0.U && !future && active
    reorder.io.enqueue.bits.generation := io.disposition.bits.generation
    reorder.io.enqueue.bits.sequence := sequence
    reorder.io.enqueue.bits.length := io.disposition.bits.length
    reorder.io.enqueue.bits.fin := io.disposition.bits.fin
    // Admission is best effort: a full/overlapping cache must not block the
    // next packet which could contain the missing prefix.
    val cache = future && reorder.io.enqueue.ready
    val space = !retain || ring.io.reserve.ready
    io.disposition.ready := state === decision && slice.io.config.ready && space
    slice.io.config.valid := state === decision && io.disposition.valid && space
    slice.io.config.bits.skip := Mux(retain || cache, io.disposition.bits.skip, 0.U)
    slice.io.config.bits.length := Mux(retain || cache, io.disposition.bits.length, 0.U)
    slice.io.config.bits.context := 0.U
    reorder.io.enqueue.valid := state === decision && io.disposition.valid && slice.io.config.ready && future
    ring.io.reserve.valid := state === decision && io.disposition.valid && slice.io.config.ready && retain
    ring.io.reserve.bits := io.disposition.bits.length
    when(io.disposition.fire) { state := copy; keep := retain; caching := cache }
    slice.io.in.valid := Mux(replaying, reorder.io.out.valid, io.payload.valid) && state === copy
    slice.io.in.bits := Mux(replaying, reorder.io.out.bits, io.payload.bits)
    io.payload.ready := slice.io.in.ready && state === copy && !replaying
    reorder.io.out.ready := slice.io.in.ready && state === copy && replaying
    val copying = keep && active
    ring.io.in.valid := slice.io.out.valid && copying
    ring.io.in.bits := slice.io.out.bits
    // An admitted write is drained even if CLEAR arrives midway; the cache
    // cancels ownership publication after it consumes the complete payload.
    reorder.io.in.valid := slice.io.out.valid && caching
    reorder.io.in.bits := slice.io.out.bits
    slice.io.out.ready := Mux(caching, reorder.io.in.ready, !copying || ring.io.in.ready)
    when(slice.io.out.fire && slice.io.out.bits.last) {
        state := idle; keep := false.B; caching := false.B
    }
    when(io.clear) { cancelled := true.B }

    io.out.valid := ring.io.out.valid && io.deliver && !io.clear
    io.out.bits := ring.io.out.bits
    ring.io.out.ready := io.out.ready && io.deliver && !io.clear
    io.window := ring.io.free
    val released = RegInit(0.U(17.W))
    val newCredit = released +& PopCount(io.out.bits.keep)
    val allRead = PopCount(io.out.bits.keep) === (words * 16).U - ring.io.free
    // Coalesce read-to-empty updates across a short burst. Immediate allRead
    // updates otherwise produce a second ACK for every tiny incoming segment
    // and can saturate the shared Ethernet control transmitter. Credit batches
    // and a zero-window reopening remain immediate; a final small read receives
    // an update after at most 32 clocks without another application read.
    val drainedPending = RegInit(false.B)
    val quiet = RegInit(0.U(6.W))
    val creditUpdate = io.out.fire &&
        (newCredit >= math.min(1460, words * 8).U || ring.io.free === 0.U)
    val drainedUpdate = drainedPending && quiet === 1.U && !io.out.fire
    val update = creditUpdate || drainedUpdate
    when(quiet =/= 0.U) { quiet := quiet - 1.U }
    when(io.out.fire) {
        released := newCredit
        drainedPending := allRead
        quiet := 32.U
    }
    when(update) { released := 0.U; drainedPending := false.B }
    when(io.clear) { released := 0.U; drainedPending := false.B; quiet := 0.U }
    io.updateWindow := RegNext(update, false.B) || reorder.io.sackUpdated
    io.sack := reorder.io.sack
    io.empty := ring.io.empty && state === idle && reorder.io.empty
    io.busy := state =/= idle
    io.reorderCount := reorder.io.count
    io.reorderDropped := io.disposition.fire && future && !cache
    val receiveFault = ring.io.fault || reorder.io.fault ||
        (io.disposition.fire && io.disposition.bits.token =/= expectedToken) ||
        (slice.io.out.fire && (copying || caching) && slice.io.out.bits.error)
    io.fault := RegNext(receiveFault, false.B)
}
