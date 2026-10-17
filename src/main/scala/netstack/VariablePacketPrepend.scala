package netstack

import chisel3._
import chisel3.util._

class VariablePrependConfig(val maxHeaderBytes: Int) extends Bundle {
    val header = UInt((maxHeaderBytes * 8).W)
    val headerLength = UInt(log2Ceil(maxHeaderBytes + 1).W)
    val length = UInt(16.W)
    val context = UInt(64.W)
}

/** Pack a variable prefix and an already validated payload. Byte zero occupies
  * the low bits. Prefix bytes beyond headerLength are ignored, even if nonzero.
  * The final beat is held until the payload's last beat has been consumed.
  */
class VariablePacketPrepend(val maxHeaderBytes: Int = 80) extends Module {
    require(maxHeaderBytes >= 32 && maxHeaderBytes <= 127)
    val io = IO(new Bundle {
        val config = Flipped(Decoupled(new VariablePrependConfig(maxHeaderBytes)))
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val out = Decoupled(new PacketBeat(16))
        val context = Output(UInt(64.W))
    })
    val width = maxHeaderBytes * 8
    val busy = RegInit(false.B)
    val remaining = Reg(UInt(16.W))
    val context = Reg(UInt(64.W))
    val reservoir = RegInit(0.U(width.W))
    val count = RegInit(0.U(log2Ceil(maxHeaderBytes + 1).W))
    val inputDone = RegInit(false.B)
    val outputDone = RegInit(false.B)
    val bad = RegInit(false.B)
    io.config.ready := !busy
    io.context := context
    io.out.valid := busy && !outputDone &&
        (count > 16.U || (count === 16.U && remaining =/= 0.U) || inputDone)
    io.out.bits.data := reservoir(127, 0)
    io.out.bits.keep := Mux(count >= 16.U, "hffff".U, ((1.U(17.W) << count(4, 0)) - 1.U)(15, 0))
    io.out.bits.last := inputDone && count <= 16.U
    io.out.bits.error := bad
    // While the long header drains, input must not append past the reservoir.
    io.in.ready := busy && !inputDone &&
        (count <= 16.U || (io.out.fire && count <= 32.U)) && (!io.out.valid || io.out.ready)
    val popped = Mux(io.out.fire, Mux(count > 16.U, 16.U, count), 0.U)
    val afterCount = count - popped
    val afterData = reservoir >> (popped << 3)
    val available = PopCount(io.in.bits.keep)
    val taken = Mux(remaining > available, available, remaining)(4, 0)
    val mask = ((1.U(129.W) << (taken << 3)) - 1.U)(127, 0)
    val legal = (io.in.bits.keep & (io.in.bits.keep + 1.U)) === 0.U
    when(io.out.fire) {
        count := afterCount
        reservoir := afterData
        when(io.out.bits.last) { outputDone := true.B }
    }
    when(io.in.fire) {
        count := afterCount + taken
        reservoir := afterData | ((io.in.bits.data & mask).pad(width) << (afterCount << 3))(width - 1, 0)
        remaining := remaining - taken
        bad := bad || io.in.bits.error || !legal || available > remaining ||
            (!io.in.bits.last && io.in.bits.keep =/= "hffff".U)
        when(io.in.bits.last) {
            inputDone := true.B
            remaining := 0.U
            bad := bad || io.in.bits.error || !legal || available =/= remaining
        }
    }
    when(busy && inputDone && outputDone) { busy := false.B }
    val maskedHeader = (0 until maxHeaderBytes).map { i =>
        (Mux(i.U < io.config.bits.headerLength, io.config.bits.header(8 * i + 7, 8 * i), 0.U)
            .pad(width) << (8 * i))(width - 1, 0)
    }.reduce(_ | _)
    when(io.config.fire) {
        busy := true.B
        remaining := io.config.bits.length
        context := io.config.bits.context
        reservoir := maskedHeader
        count := Mux(io.config.bits.headerLength > maxHeaderBytes.U, maxHeaderBytes.U, io.config.bits.headerLength)
        inputDone := false.B
        outputDone := false.B
        bad := io.config.bits.headerLength > maxHeaderBytes.U
    }
}
