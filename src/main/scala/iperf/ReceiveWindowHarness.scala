package iperf

import chisel3._
import chisel3.util._
import netstack._

/** Actual receive queue with a small validated-metadata/TCB test peer.
  * The peer supplies dispositions; payload storage and replay are production RTL.
  */
class ReceiveWindowHarness extends Module {
    val io = IO(new Bundle {
        val clear = Input(Bool())
        val offerValid = Input(Bool()); val offerReady = Output(Bool())
        val sequence = Input(UInt(32.W)); val length = Input(UInt(16.W))
        val inValid = Input(Bool()); val inReady = Output(Bool())
        val inLo = Input(UInt(64.W)); val inHi = Input(UInt(64.W))
        val inKeep = Input(UInt(16.W)); val inLast = Input(Bool())
        val outReady = Input(Bool()); val outValid = Output(Bool())
        val outLo = Output(UInt(64.W)); val outHi = Output(UInt(64.W)); val outKeep = Output(UInt(16.W))
        val free = Output(UInt(32.W)); val empty = Output(Bool()); val fault = Output(Bool())
        val update = Output(Bool()); val next = Output(UInt(32.W))
        val sackLength = Output(UInt(6.W)); val sackLeft = Output(UInt(32.W)); val sackRight = Output(UInt(32.W))
    })
    val queue = Module(new TcpReceiveQueue(16384))
    val next = RegInit(0.U(32.W))
    val generation = RegInit(1.U(32.W))
    val pending = RegInit(false.B)
    val disposition = Reg(new TcpRxDisposition)
    queue.io.metadata.valid := io.offerValid && !io.clear
    queue.io.metadata.bits := 0.U.asTypeOf(new TcpSegment)
    queue.io.metadata.bits.sequence := io.sequence
    queue.io.metadata.bits.payloadLength := io.length
    queue.io.metadata.bits.flags := 16.U
    io.offerReady := queue.io.metadata.ready && !io.clear
    queue.io.rx.ready := !pending && !io.clear
    queue.io.disposition.valid := pending && !io.clear
    queue.io.disposition.bits := disposition
    when(queue.io.rx.fire) {
        val segment = queue.io.rx.bits.segment
        val ordered = segment.sequence === next
        disposition := 0.U.asTypeOf(new TcpRxDisposition)
        disposition.generation := generation
        disposition.token := queue.io.rx.bits.payloadToken
        disposition.keep := true.B
        disposition.length := segment.payloadLength
        disposition.outOfOrder := !ordered
        pending := true.B
        when(ordered) { next := next + segment.payloadLength }
    }
    when(queue.io.disposition.fire) { pending := false.B }
    when(io.clear) { pending := false.B; next := 0.U; generation := generation + 1.U }
    queue.io.receiveNext := next; queue.io.generation := generation
    queue.io.credit := queue.io.window; queue.io.enabled := true.B
    queue.io.deliver := true.B; queue.io.clear := io.clear
    queue.io.payload.valid := io.inValid
    queue.io.payload.bits.data := Cat(io.inHi, io.inLo)
    queue.io.payload.bits.keep := io.inKeep; queue.io.payload.bits.last := io.inLast
    queue.io.payload.bits.error := false.B
    io.inReady := queue.io.payload.ready
    queue.io.out.ready := io.outReady
    io.outValid := queue.io.out.valid; io.outLo := queue.io.out.bits.data(63, 0)
    io.outHi := queue.io.out.bits.data(127, 64); io.outKeep := queue.io.out.bits.keep
    io.free := queue.io.window; io.empty := queue.io.empty; io.fault := queue.io.fault
    io.update := queue.io.updateWindow; io.next := next
    io.sackLength := TcpSackEncoding(queue.io.sack, next, true.B)._1
    io.sackLeft := queue.io.sack.left(0); io.sackRight := queue.io.sack.right(0)
}
