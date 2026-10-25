package netstack

import chisel3._
import chisel3.util._

/** Two packet banks with delayed commit, synchronous RAM and an elastic reader.
  * One verdict is required after each accepted last beat. A true verdict commits
  * the packet; false releases it without exposing bytes at the output.
  * Oversize packets are drained to last and dropped even with a true verdict.
  * The writer may fill one bank while the reader drains the other.
  * Packet error is also sticky and forces a drop. No reset clears RAM contents:
  * ownership flags make stale bytes unreachable.
  */
class PacketCommitBuffer(val bytes: Int = 16, val beatsPerPacket: Int = 1024) extends Module {
    require(isPow2(beatsPerPacket) && beatsPerPacket >= 2)
    val io = IO(new Bundle {
        val in = Flipped(Decoupled(new PacketBeat(bytes)))
        val verdict = Flipped(Decoupled(Bool()))
        val inContext = Input(UInt(64.W))
        val out = Decoupled(new PacketBeat(bytes))
        val outContext = Output(UInt(64.W))
        val droppedContext = Output(UInt(64.W))
        val dropped = Output(Bool())
    })
    val indexBits = log2Ceil(beatsPerPacket)
    val memory = SyncReadMem(2 * beatsPerPacket, new PacketBeat(bytes))
    val occupied = RegInit(VecInit(Seq.fill(2)(false.B)))
    val committed = RegInit(VecInit(Seq.fill(2)(false.B)))
    val sizes = Reg(Vec(2, UInt((indexBits + 1).W)))
    val contexts = Reg(Vec(2, UInt(64.W)))
    val writeBank = RegInit(false.B)
    val writeCount = RegInit(0.U((indexBits + 1).W))
    val receiving = RegInit(false.B)
    val awaiting = RegInit(false.B)
    val poisoned = RegInit(false.B)
    io.in.ready := !awaiting && (receiving || !occupied(writeBank))
    io.verdict.ready := awaiting
    io.dropped := io.verdict.fire && (!io.verdict.bits || poisoned)
    io.droppedContext := contexts(writeBank)
    val overflow = writeCount === beatsPerPacket.U
    when(io.in.fire) {
        when(!receiving) { contexts(writeBank) := io.inContext }
        occupied(writeBank) := true.B
        receiving := !io.in.bits.last
        poisoned := poisoned || io.in.bits.error || overflow
        when(!overflow) {
            memory.write(Cat(writeBank, writeCount(indexBits - 1, 0)), io.in.bits)
            writeCount := writeCount + 1.U
        }
        when(io.in.bits.last) {
            awaiting := true.B
            sizes(writeBank) := Mux(overflow, writeCount, writeCount + 1.U)
        }
    }
    when(io.verdict.fire) {
        awaiting := false.B
        writeCount := 0.U
        poisoned := false.B
        when(io.verdict.bits && !poisoned) {
            committed(writeBank) := true.B
            writeBank := !writeBank
        }.otherwise {
            occupied(writeBank) := false.B
        }
    }

    val readBank = RegInit(false.B)
    io.outContext := contexts(readBank)
    val readIndex = RegInit(0.U(indexBits.W))
    val issuedLast = RegInit(false.B)
    val output = Module(new MemoryQueue(new PacketBeat(bytes), 2))
    io.out <> output.io.deq
    val pending = RegInit(false.B)
    // Account for the read already in flight as well as queued words.
    val room = (output.io.count +& pending.asUInt) < 2.U || output.io.deq.fire
    val issue = committed(readBank) && !issuedLast && room
    val readData = memory.read(Cat(readBank, readIndex), issue)
    pending := issue
    output.io.enq.valid := pending
    output.io.enq.bits := readData
    when(issue) {
        readIndex := readIndex + 1.U
        when((readIndex +& 1.U) === sizes(readBank)) { issuedLast := true.B }
    }
    when(io.out.fire && io.out.bits.last) {
        occupied(readBank) := false.B
        committed(readBank) := false.B
        readBank := !readBank
        readIndex := 0.U
        issuedLast := false.B
    }
}

class BufferHarness extends Module {
    val io = IO(new Bundle {
        val lo = Input(UInt(64.W))
        val hi = Input(UInt(64.W))
        val keep = Input(UInt(16.W))
        val last = Input(Bool())
        val error = Input(Bool())
        val valid = Input(Bool())
        val ready = Output(Bool())
        val verdictValid = Input(Bool())
        val verdictReady = Output(Bool())
        val accept = Input(Bool())
        val dropped = Output(Bool())
        val outReady = Input(Bool())
        val outValid = Output(Bool())
        val outLo = Output(UInt(64.W))
        val outHi = Output(UInt(64.W))
        val outKeep = Output(UInt(16.W))
        val outLast = Output(Bool())
    })
    val dut = Module(new PacketCommitBuffer(16, 16))
    dut.io.in.bits.data := Cat(io.hi, io.lo)
    dut.io.in.bits.keep := io.keep
    dut.io.in.bits.last := io.last
    dut.io.in.bits.error := io.error
    dut.io.in.valid := io.valid
    dut.io.inContext := 0.U
    io.ready := dut.io.in.ready
    dut.io.verdict.valid := io.verdictValid
    dut.io.verdict.bits := io.accept
    io.verdictReady := dut.io.verdict.ready
    io.dropped := dut.io.dropped
    dut.io.out.ready := io.outReady
    io.outValid := dut.io.out.valid
    io.outLo := dut.io.out.bits.data(63, 0)
    io.outHi := dut.io.out.bits.data(127, 64)
    io.outKeep := dut.io.out.bits.keep
    io.outLast := dut.io.out.bits.last
}
