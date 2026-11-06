package iperf

import chisel3._
import chisel3.util._
import netstack._
import soc.ip.ethernet._

class FrameIdentity extends Bundle {
    val tcp = Bool()
    val tag = UInt(32.W)
    val generation = UInt(32.W)
}

/** Two-bank physical TX and an ordered TCP outcome scoreboard.
  * Late route failures wait for earlier wire outcomes; acceptance is never
  * interpreted as transmission. Incoming rejects and older bank completions
  * retain separate owners, including simultaneous rejection/completion.
  */
class MacTransmit extends Module {
    val io = IO(new Bundle {
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val tcp = Input(Bool()); val tag = Input(UInt(32.W)); val generation = Input(UInt(32.W))
        val routed = Flipped(Valid(new TcpWireResult))
        val completed = Valid(new TcpWireResult)
        val link = Input(Bool())
        val data = Output(UInt(8.W)); val enable = Output(Bool()); val error = Output(Bool())
        val completedFrames = Output(UInt(64.W))
        val physicalDone = Output(Bool()); val rejected = Output(Bool())
    })
    val unpack = Module(new MacUnpack)
    unpack.io.in <> io.in
    val tx = Module(new QueuedGmiiFrameTx(2048, 2))
    val identities = Module(new Queue(new FrameIdentity, 4))
    val first = RegInit(true.B)
    val identity = Reg(new FrameIdentity)
    when(io.in.fire) {
        when(first) { identity.tcp := io.tcp; identity.tag := io.tag; identity.generation := io.generation }
        first := io.in.bits.last
    }
    tx.io.enable := io.link
    tx.io.byteStep.get := true.B
    tx.io.abort.get := !io.link
    tx.io.frame.valid := unpack.io.out.valid && identities.io.enq.ready
    tx.io.frame.bits := unpack.io.out.bits
    unpack.io.out.ready := tx.io.frame.ready && identities.io.enq.ready
    io.data := tx.io.gmiiData; io.enable := tx.io.gmiiEnable; io.error := tx.io.gmiiError
    io.physicalDone := tx.io.done; io.rejected := tx.io.rejected
    val droppingStored = !io.link && identities.io.deq.valid
    val endIncoming = tx.io.frame.fire && tx.io.frame.bits.last
    identities.io.enq.valid := endIncoming && !tx.io.rejected
    identities.io.enq.bits := identity
    identities.io.deq.ready := tx.io.done || droppingStored
    assert(!tx.io.done || identities.io.deq.valid)
    // The board has two registered pad stages. Three cycles put completion
    // strictly after the last FCS byte has left that boundary.
    val outcome = Wire(Valid(new TcpWireResult))
    outcome.valid := identities.io.deq.fire && identities.io.deq.bits.tcp
    outcome.bits.tag := identities.io.deq.bits.tag
    outcome.bits.generation := identities.io.deq.bits.generation
    outcome.bits.failed := droppingStored
    val physical = (0 until 3).foldLeft(outcome) { (previous, _) =>
        val stage = Wire(Valid(new TcpWireResult))
        stage.valid := RegNext(previous.valid, false.B)
        stage.bits := RegEnable(previous.bits, previous.valid)
        stage
    }
    val rejectedIncoming = endIncoming && tx.io.rejected && !droppingStored && identity.tcp

    val routes = Module(new Queue(new TcpWireResult, 64))
    routes.io.enq.valid := io.routed.valid
    routes.io.enq.bits := io.routed.bits
    assert(!io.routed.valid || routes.io.enq.ready, "TCP route ledger overflow")
    val ready = RegInit(VecInit(Seq.fill(64)(false.B)))
    val failed = Reg(Vec(64, Bool()))
    val tags = Reg(Vec(64, UInt(32.W)))
    val generations = Reg(Vec(64, UInt(32.W)))
    val head = routes.io.deq.bits
    val headIndex = head.tag(5,0)
    val matched = ready(headIndex) && tags(headIndex) === head.tag && generations(headIndex) === head.generation
    io.completed.valid := routes.io.deq.valid && (head.failed || matched)
    io.completed.bits := head
    io.completed.bits.failed := head.failed || failed(headIndex)
    routes.io.deq.ready := io.completed.valid
    when(io.completed.valid && !head.failed) { ready(headIndex) := false.B }
    def record(valid: Bool, tag: UInt, generation: UInt, bad: Bool): Unit = {
        val index = tag(5,0)
        when(valid) {
            assert(!ready(index), "TCP physical outcome overwrote a live owner")
            ready(index) := true.B; tags(index) := tag; generations(index) := generation; failed(index) := bad
        }
    }
    record(physical.valid, physical.bits.tag, physical.bits.generation, physical.bits.failed)
    record(rejectedIncoming, identity.tag, identity.generation, true.B)
    val count = RegInit(0.U(64.W))
    when(tx.io.done) { count := count + 1.U }
    io.completedFrames := count
}
