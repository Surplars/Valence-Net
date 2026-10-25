package netstack

import chisel3._
import chisel3.util._

/** Queue with the same non-flowing, non-pipelined handshake as Queue.
  * Deep wide entries use synchronous RAM. Shallow queues retain Queue
  * inference so constant control fields can be eliminated.
  * The next head is prefetched on dequeue;
  * explicit collision forwarding defines empty-to-nonempty and wraparound
  * behavior independently of the FPGA RAM's cross-port collision mode.
  */
class MemoryQueue[T <: Data](gen: T, val entries: Int, val forceRam: Boolean = false) extends Module {
    require(entries >= 1)
    val io = IO(new QueueIO(gen, entries))
    if (entries >= 2 && (forceRam || entries >= 8) && gen.getWidth >= 96) {
        val bits = math.max(1, log2Ceil(entries))
        val memory = SyncReadMem(entries, gen).suggestName("bram_fifo")
        val enqueue = RegInit(0.U(bits.W))
        val dequeue = RegInit(0.U(bits.W))
        val count = RegInit(0.U(log2Ceil(entries + 1).W))
        def next(p: UInt): UInt = Mux(p === (entries - 1).U, 0.U, p + 1.U)
        io.enq.ready := count =/= entries.U
        io.deq.valid := count =/= 0.U
        io.count := count
        when(io.enq.fire) { memory.write(enqueue, io.enq.bits); enqueue := next(enqueue) }
        when(io.deq.fire) { dequeue := next(dequeue) }
        when(io.enq.fire =/= io.deq.fire) {
            count := Mux(io.enq.fire, count + 1.U, count - 1.U)
        }
        val readAddress = Mux(io.deq.fire, next(dequeue), dequeue)
        val data = memory.read(readAddress, true.B)
        val collision = io.enq.fire && enqueue === readAddress
        val forwarded = RegNext(collision, false.B)
        val written = RegEnable(io.enq.bits, collision)
        io.deq.bits := Mux(forwarded, written, data)
    } else {
        val queue = Module(new Queue(gen, entries))
        io <> queue.io
    }
}

class MemoryQueueHarness extends Module {
    val io = IO(new Bundle {
        val select = Input(UInt(2.W))
        val valid = Input(Bool()); val ready = Output(Bool())
        val lo = Input(UInt(64.W)); val hi = Input(UInt(64.W))
        val outReady = Input(Bool()); val outValid = Output(Bool())
        val outLo = Output(UInt(64.W)); val outHi = Output(UInt(64.W))
        val count = Output(UInt(5.W))
    })
    val queues = Seq(2, 3, 16).zipWithIndex.map { case (depth, i) =>
        val q = Module(new MemoryQueue(UInt(128.W), depth, forceRam = true))
        q.io.enq.valid := io.valid && io.select === i.U
        q.io.enq.bits := Cat(io.hi, io.lo)
        q.io.deq.ready := io.outReady && io.select === i.U
        q
    }
    io.ready := VecInit(queues.map(_.io.enq.ready))(io.select)
    io.outValid := VecInit(queues.map(_.io.deq.valid))(io.select)
    io.outLo := VecInit(queues.map(_.io.deq.bits(63, 0)))(io.select)
    io.outHi := VecInit(queues.map(_.io.deq.bits(127, 64)))(io.select)
    io.count := VecInit(queues.map(q => q.io.count.pad(5)))(io.select)
}
