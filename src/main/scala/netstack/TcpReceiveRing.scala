package netstack

import chisel3._
import chisel3.util._

/** Byte-credit receive ring. A reservation owns capacity before bytes are ACKed;
  * only written bytes can be read. Sixteen independently addressed byte lanes
  * pack arbitrary segment lengths without spending a whole bank on short TCP
  * segments. All pointers wrap within the configured power-of-two capacity.
  *
  * Input follows one nonempty reservation at a time, with validated packed
  * bytes. Output is a byte stream: every beat is a chunk (last=true), NOT a TCP
  * packet boundary or FIN. Reset cancels reservations and queued application
  * data together. The consumer must treat connection abort as cancellation.
  */
class TcpReceiveRing(val words: Int = 4096) extends Module {
    require(isPow2(words) && words >= 4 && words <= 4096)
    val capacity = words * 16
    val pointerBits = log2Ceil(capacity)
    val io = IO(new Bundle {
        val reserve = Flipped(Decoupled(UInt(16.W)))
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val out = Decoupled(new PacketBeat(16))
        val free = Output(UInt((pointerBits + 1).W))
        val empty = Output(Bool())
        val fault = Output(Bool())
    })
    val free = RegInit(capacity.U((pointerBits + 1).W))
    val available = RegInit(0.U((pointerBits + 1).W))
    val remaining = RegInit(0.U(16.W))
    val writePointer = RegInit(0.U(pointerBits.W))
    val readPointer = RegInit(0.U(pointerBits.W))
    val output = Module(new MemoryQueue(new PacketBeat(16), 2))
    val pending = RegInit(false.B)
    val pendingOffset = Reg(UInt(4.W))
    val pendingCount = Reg(UInt(5.W))
    val inputCount = PopCount(io.in.bits.keep)
    val outputCount = PopCount(io.out.bits.keep)
    io.free := free
    io.empty := free === capacity.U
    io.reserve.ready := remaining === 0.U && io.reserve.bits =/= 0.U && io.reserve.bits <= io.free
    io.in.ready := remaining =/= 0.U
    val legal = (io.in.bits.keep & (io.in.bits.keep + 1.U)) === 0.U
    io.fault := io.in.fire && (!legal || inputCount === 0.U || inputCount > remaining ||
        io.in.bits.error || io.in.bits.last =/= (inputCount === remaining) ||
        (!io.in.bits.last && io.in.bits.keep =/= "hffff".U))
    when(io.reserve.fire) { remaining := io.reserve.bits }
    when(io.in.fire) { remaining := remaining - inputCount; writePointer := writePointer + inputCount }
    free := free - Mux(io.reserve.fire, io.reserve.bits, 0.U) + Mux(io.out.fire, outputCount, 0.U)

    val room = (output.io.count +& pending.asUInt) < 2.U || output.io.deq.fire
    val issue = available =/= 0.U && room
    val count = Mux(available > 16.U, 16.U, available)(4, 0)
    available := available + Mux(io.in.fire, inputCount, 0.U) - Mux(issue, count, 0.U)
    pending := issue
    when(issue) { readPointer := readPointer + count; pendingOffset := readPointer(3, 0); pendingCount := count }
    val rotateInput = (Cat(io.in.bits.data, io.in.bits.data) << (writePointer(3, 0) << 3))(255, 128)
    val readBytes = Wire(Vec(16, UInt(8.W)))
    for (i <- 0 until 16) {
        val memory = SyncReadMem(words, UInt(8.W))
        val writeAddress = (writePointer(pointerBits - 1, 4) + (i.U < writePointer(3, 0)).asUInt)(log2Ceil(words) - 1, 0)
        val readAddress = (readPointer(pointerBits - 1, 4) + (i.U < readPointer(3, 0)).asUInt)(log2Ceil(words) - 1, 0)
        val index = (i.U(5.W) - writePointer(3, 0))(3, 0)
        when(io.in.fire && index < inputCount) { memory.write(writeAddress, rotateInput(8 * i + 7, 8 * i)) }
        readBytes(i) := memory.read(readAddress, issue)
    }
    val packed = readBytes.asUInt
    output.io.enq.valid := pending
    output.io.enq.bits.data := (Cat(packed, packed) >> (pendingOffset << 3))(127, 0)
    output.io.enq.bits.keep := VecInit((0 until 16).map(i => i.U < pendingCount)).asUInt
    output.io.enq.bits.last := true.B
    output.io.enq.bits.error := false.B
    io.out <> output.io.deq
}

class TcpRingHarness extends Module {
    val io = IO(new Bundle {
        val reserveValid = Input(Bool()); val reserveReady = Output(Bool()); val length = Input(UInt(16.W))
        val valid = Input(Bool()); val ready = Output(Bool())
        val lo = Input(UInt(64.W)); val hi = Input(UInt(64.W)); val keep = Input(UInt(16.W)); val last = Input(Bool())
        val outValid = Output(Bool()); val outReady = Input(Bool())
        val outLo = Output(UInt(64.W)); val outHi = Output(UInt(64.W)); val outKeep = Output(UInt(16.W))
        val free = Output(UInt(11.W)); val empty = Output(Bool()); val fault = Output(Bool())
    })
    val dut = Module(new TcpReceiveRing(64))
    dut.io.reserve.valid := io.reserveValid; dut.io.reserve.bits := io.length; io.reserveReady := dut.io.reserve.ready
    dut.io.in.valid := io.valid; dut.io.in.bits.data := Cat(io.hi, io.lo); dut.io.in.bits.keep := io.keep
    dut.io.in.bits.last := io.last; dut.io.in.bits.error := false.B; io.ready := dut.io.in.ready
    dut.io.out.ready := io.outReady; io.outValid := dut.io.out.valid
    io.outLo := dut.io.out.bits.data(63, 0); io.outHi := dut.io.out.bits.data(127, 64); io.outKeep := dut.io.out.bits.keep
    io.free := dut.io.free; io.empty := dut.io.empty; io.fault := dut.io.fault
}
