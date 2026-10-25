package netstack

import chisel3._
import chisel3.util._

/** Two committed payload banks decouple a shared validator from one busy TCB.
  * Metadata is committed with its complete payload. A stale epoch is drained
  * locally and retired exactly once, including while a prior notice is blocked.
  */
class TcpValidatedReceiveBuffer(val packetBeats: Int = 128) extends Module {
    class Metadata extends Bundle {
        val segment = new TcpSegment
        val generation = UInt(32.W)
    }
    val io = IO(new Bundle {
        val inMetadata = Flipped(Decoupled(new Metadata))
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val generation = Input(UInt(32.W))
        val metadata = Decoupled(new TcpSegment)
        val out = Decoupled(new PacketBeat(16))
        val dropped = Output(UInt(2.W))
        val empty = Output(Bool())
    })
    val buffer = Module(new PacketCommitBuffer(16, packetBeats))
    val metadata = Module(new MemoryQueue(new Metadata, 2))
    val writing = RegInit(false.B)
    val verdict = RegInit(false.B)
    val descriptor = Reg(new Metadata)
    io.inMetadata.ready := !writing && !verdict && buffer.io.in.ready
    when(io.inMetadata.fire) { descriptor := io.inMetadata.bits; writing := true.B }
    buffer.io.inContext := 0.U
    buffer.io.in.valid := io.in.valid && writing
    buffer.io.in.bits := io.in.bits
    io.in.ready := writing && buffer.io.in.ready
    when(io.in.fire && io.in.bits.last) { writing := false.B; verdict := true.B }
    buffer.io.verdict.valid := verdict && metadata.io.enq.ready
    buffer.io.verdict.bits := true.B
    metadata.io.enq.valid := verdict && buffer.io.verdict.ready && !buffer.io.dropped
    metadata.io.enq.bits := descriptor
    when(buffer.io.verdict.fire) { verdict := false.B }

    val reading = RegInit(false.B)
    val discard = RegInit(false.B)
    val stale = metadata.io.deq.bits.generation =/= io.generation
    val offer = metadata.io.deq.valid && buffer.io.out.valid && !reading
    io.metadata.valid := offer && !stale
    io.metadata.bits := metadata.io.deq.bits.segment
    metadata.io.deq.ready := buffer.io.out.valid && !reading && (stale || io.metadata.ready)
    when(metadata.io.deq.fire) { reading := true.B; discard := stale }
    io.out.valid := buffer.io.out.valid && reading && !discard
    io.out.bits := buffer.io.out.bits
    buffer.io.out.ready := reading && (discard || io.out.ready)
    when(buffer.io.out.fire && buffer.io.out.bits.last) { reading := false.B }
    io.dropped := buffer.io.dropped.asUInt +& (metadata.io.deq.fire && stale).asUInt
    io.empty := !writing && !verdict && !reading && !metadata.io.deq.valid
}
