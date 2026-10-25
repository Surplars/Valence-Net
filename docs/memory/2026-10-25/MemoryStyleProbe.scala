package netstack
import chisel3._
import chisel3.util._
import chisel3.experimental.annotate
import _root_.circt.stage.ChiselStage

class BlockQueueProbe extends Queue(UInt(146.W), 2, useSyncReadMem = true) {
}
class UltraStoreProbe extends Module {
    val io = IO(new Bundle {
        val wa = Input(UInt(10.W)); val wd = Input(UInt(128.W)); val we = Input(Bool())
        val ra = Input(UInt(10.W)); val re = Input(Bool()); val rd = Output(UInt(128.W))
    })
    val memory = SyncReadMem(1024, UInt(128.W))
    when(io.we) { memory.write(io.wa, io.wd) }
    io.rd := memory.read(io.ra, io.re)
}
object MemoryStyleProbe extends App {
    ChiselStage.emitSystemVerilogFile(new BlockQueueProbe, Array("--target-dir","build/block"))
    ChiselStage.emitSystemVerilogFile(new UltraStoreProbe, Array("--target-dir","build/ultra"))
}
