package netstack

import chisel3._
import chisel3.util._

class SliceConfig extends Bundle {
    val skip = UInt(16.W)
    val length = UInt(16.W)
    val context = UInt(128.W)
}

/** Remove a prefix, retain exactly length bytes, then drain link-layer padding.
  * Input nonfinal beats are full; keep is a low-bit prefix. The upstream packet
  * validator must enforce this contract. A short input terminates with error
  * rather than hanging. Final output waits for input last, including padding,
  * so late stream errors cannot be lost. Empty payloads use keep=0,last=true.
  */
class PacketSlice extends Module {
    val io = IO(new Bundle {
        val config = Flipped(Decoupled(new SliceConfig))
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val out = Decoupled(new PacketBeat(16))
        val context = Output(UInt(128.W))
    })
    val busy = RegInit(false.B)
    val skip = Reg(UInt(16.W))
    val remaining = Reg(UInt(16.W))
    val context = Reg(UInt(128.W))
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
    val discarded = Mux(skip > available, available, skip)(4, 0)
    val usable = available - discarded
    val taken = Mux(remaining > usable, usable, remaining)(4, 0)
    // The data shift depends only on the registered prefix remainder. Decide
    // each retained byte in parallel, instead of chaining available/min/sub/
    // min before the barrel shifter. A skip of at least one full beat retains
    // no bytes; otherwise i+skip < available is exactly i < usable.
    val inputMask = VecInit((0 until 16).map(i => Fill(8,
        skip < 16.U && i.U < remaining &&
            (skip(3, 0) +& i.U) < available))).asUInt
    val selected = (io.in.bits.data >> (skip(3, 0) << 3)) & inputMask

    when(io.out.fire) {
        count := afterCount
        reservoir := afterData
        when(io.out.bits.last) { outputDone := true.B }
    }
    when(io.in.fire) {
        count := afterCount + taken
        reservoir := afterData | (selected.pad(256) << (afterCount << 3))(255, 0)
        skip := skip - discarded
        remaining := remaining - taken
        bad := bad || io.in.bits.error
        when(io.in.bits.last) {
            inputDone := true.B
            remaining := 0.U
            bad := bad || io.in.bits.error || remaining =/= taken || skip =/= discarded
        }
    }
    when(busy && inputDone && outputDone) { busy := false.B }
    when(io.config.fire) {
        busy := true.B
        skip := io.config.bits.skip
        remaining := io.config.bits.length
        context := io.config.bits.context
        reservoir := 0.U
        count := 0.U
        inputDone := false.B
        outputDone := false.B
        bad := false.B
    }
}

class SliceHarness extends Module {
    val io = IO(new Bundle {
        val configValid = Input(Bool())
        val configReady = Output(Bool())
        val skip = Input(UInt(7.W))
        val length = Input(UInt(16.W))
        val context = Input(UInt(64.W))
        val lo = Input(UInt(64.W))
        val hi = Input(UInt(64.W))
        val keep = Input(UInt(16.W))
        val last = Input(Bool())
        val error = Input(Bool())
        val valid = Input(Bool())
        val ready = Output(Bool())
        val outReady = Input(Bool())
        val outValid = Output(Bool())
        val outLo = Output(UInt(64.W))
        val outHi = Output(UInt(64.W))
        val outKeep = Output(UInt(16.W))
        val outLast = Output(Bool())
        val outError = Output(Bool())
        val outContext = Output(UInt(64.W))
    })
    val dut = Module(new PacketSlice)
    dut.io.config.valid := io.configValid
    dut.io.config.bits.skip := io.skip
    dut.io.config.bits.length := io.length
    dut.io.config.bits.context := io.context
    io.configReady := dut.io.config.ready
    dut.io.in.valid := io.valid
    dut.io.in.bits.data := Cat(io.hi, io.lo)
    dut.io.in.bits.keep := io.keep
    dut.io.in.bits.last := io.last
    dut.io.in.bits.error := io.error
    io.ready := dut.io.in.ready
    dut.io.out.ready := io.outReady
    io.outValid := dut.io.out.valid
    io.outLo := dut.io.out.bits.data(63, 0)
    io.outHi := dut.io.out.bits.data(127, 64)
    io.outKeep := dut.io.out.bits.keep
    io.outLast := dut.io.out.bits.last
    io.outError := dut.io.out.bits.error
    io.outContext := dut.io.context(63, 0)
}
