package netstack

import chisel3._
import chisel3.util._

class PrependConfig(val headerBytes: Int) extends Bundle {
    val header = UInt((headerBytes * 8).W)
    val length = UInt(16.W)
    val context = UInt(64.W)
}

/** Pack a fixed-size header and a validated payload into full 128-bit beats.
  * An empty payload has a single keep=0,last=true beat. Inputs have full
  * nonfinal keeps. The caller validates the payload before this streaming
  * stage; a length violation or stream error is nevertheless marked on last.
  */
class PacketPrepend(val headerBytes: Int) extends Module {
    require(headerBytes >= 1 && headerBytes <= 32)
    val io = IO(new Bundle {
        val config = Flipped(Decoupled(new PrependConfig(headerBytes)))
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val out = Decoupled(new PacketBeat(16))
        val context = Output(UInt(64.W))
    })
    val busy = RegInit(false.B)
    val remaining = Reg(UInt(16.W))
    val context = Reg(UInt(64.W))
    val reservoir = RegInit(0.U(256.W))
    val count = RegInit(0.U(6.W))
    val inputDone = RegInit(false.B)
    val outputDone = RegInit(false.B)
    val bad = RegInit(false.B)
    io.config.ready := !busy
    io.context := context
    io.out.valid := busy && !outputDone &&
        (count > 16.U || (count === 16.U && remaining =/= 0.U) || inputDone)
    io.out.bits.data := reservoir(127, 0)
    io.out.bits.keep := Mux(count >= 16.U, "hffff".U, ((1.U(17.W) << count) - 1.U)(15, 0))
    io.out.bits.last := inputDone && count <= 16.U
    io.out.bits.error := bad
    io.in.ready := busy && !inputDone && (count <= 16.U || io.out.fire) && (!io.out.valid || io.out.ready)
    val popped = Mux(io.out.fire, Mux(count > 16.U, 16.U, count), 0.U)
    val afterCount = Mux(io.out.fire, Mux(count > 16.U, count - 16.U, 0.U), count)
    val afterData = Mux(io.out.fire, Mux(count > 16.U, reservoir >> 128, 0.U), reservoir)
    val available = PopCount(io.in.bits.keep)
    val taken = Mux(remaining > available, available, remaining)(4, 0)
    val mask = VecInit((0 until 16).map(i => Fill(8, i.U < taken))).asUInt
    when(io.out.fire) {
        count := afterCount
        reservoir := afterData
        when(io.out.bits.last) { outputDone := true.B }
    }
    when(io.in.fire) {
        count := afterCount + taken
        reservoir := afterData | ((io.in.bits.data & mask).pad(256) << (afterCount << 3))(255, 0)
        remaining := remaining - taken
        bad := bad || io.in.bits.error || available > remaining
        when(io.in.bits.last) {
            inputDone := true.B
            remaining := 0.U
            bad := bad || io.in.bits.error || available =/= remaining
        }
    }
    when(busy && inputDone && outputDone) { busy := false.B }
    when(io.config.fire) {
        busy := true.B
        remaining := io.config.bits.length
        context := io.config.bits.context
        reservoir := io.config.bits.header
        count := headerBytes.U
        inputDone := false.B
        outputDone := false.B
        bad := false.B
    }
}
