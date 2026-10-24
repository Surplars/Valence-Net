package netstack

import chisel3._
import chisel3.util._

/** Direct snapshot probe; the software peer supplies independent interval expectations. */
class TcpSackSnapshotHarness extends Module {
    val io = IO(new Bundle {
        val write = Input(Bool()); val index = Input(UInt(3.W))
        val left = Input(UInt(32.W)); val right = Input(UInt(32.W)); val age = Input(UInt(4.W))
        val valid = Input(UInt(8.W)); val base = Input(UInt(32.W))
        val changed = Input(Bool()); val clear = Input(Bool())
        val count = Output(UInt(3.W)); val busy = Output(Bool()); val updated = Output(Bool())
        val left0 = Output(UInt(32.W)); val left1 = Output(UInt(32.W))
        val left2 = Output(UInt(32.W)); val left3 = Output(UInt(32.W))
        val right0 = Output(UInt(32.W)); val right1 = Output(UInt(32.W))
        val right2 = Output(UInt(32.W)); val right3 = Output(UInt(32.W))
    })
    val left = Reg(Vec(8, UInt(32.W))); val right = Reg(Vec(8, UInt(32.W)))
    val age = Reg(Vec(8, UInt(4.W)))
    when(io.write) { left(io.index) := io.left; right(io.index) := io.right; age(io.index) := io.age }
    val snapshot = Module(new TcpSackSnapshot(8))
    snapshot.io.valid := VecInit(io.valid.asBools); snapshot.io.left := left; snapshot.io.right := right
    snapshot.io.age := age; snapshot.io.base := io.base
    snapshot.io.changed := io.changed; snapshot.io.clear := io.clear
    io.busy := snapshot.io.busy; io.updated := snapshot.io.updated; io.count := snapshot.io.blocks.count
    io.left0 := snapshot.io.blocks.left(0); io.left1 := snapshot.io.blocks.left(1)
    io.left2 := snapshot.io.blocks.left(2); io.left3 := snapshot.io.blocks.left(3)
    io.right0 := snapshot.io.blocks.right(0); io.right1 := snapshot.io.blocks.right(1)
    io.right2 := snapshot.io.blocks.right(2); io.right3 := snapshot.io.blocks.right(3)
}
