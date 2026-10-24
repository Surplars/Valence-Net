package netstack

import chisel3._
import chisel3.util._
import _root_.circt.stage.ChiselStage
import java.nio.file.{Files, Paths}

class IcmpErrorsHarness extends Module {
    val io = IO(new Bundle {
        val lo = Input(UInt(64.W)); val hi = Input(UInt(64.W))
        val keep = Input(UInt(16.W)); val last = Input(Bool()); val error = Input(Bool())
        val valid = Input(Bool()); val ready = Output(Bool())
        val localIp = Input(UInt(32.W)); val netmask = Input(UInt(32.W))
        val generate = Input(Bool()); val code = Input(UInt(8.W)); val tick = Input(Bool())
        val eventReady = Input(Bool()); val eventValid = Output(Bool())
        val routerIp = Output(UInt(32.W)); val sourceIp = Output(UInt(32.W)); val destinationIp = Output(UInt(32.W))
        val protocol = Output(UInt(8.W)); val sourcePort = Output(UInt(16.W)); val destinationPort = Output(UInt(16.W))
        val sequence = Output(UInt(32.W)); val totalLength = Output(UInt(16.W))
        val identification = Output(UInt(16.W)); val dontFragment = Output(Bool())
        val nextHopMtu = Output(UInt(16.W)); val icmpType = Output(UInt(8.W))
        val icmpCode = Output(UInt(8.W)); val pointer = Output(UInt(8.W))
        val outReady = Input(Bool()); val outValid = Output(Bool())
        val outLo = Output(UInt(64.W)); val outHi = Output(UInt(64.W))
        val outKeep = Output(UInt(16.W)); val outLast = Output(Bool())
        val outDestination = Output(UInt(32.W)); val rxDropped = Output(UInt(2.W))
        val txDropped = Output(Bool()); val txSuppressed = Output(Bool())
    })
    val rx = Module(new Ipv4IcmpErrorReceive(32))
    val tx = Module(new Ipv4IcmpErrorTransmit)
    rx.io.localIp := io.localIp; rx.io.netmask := io.netmask
    tx.io.localIp := io.localIp; tx.io.netmask := io.netmask
    tx.io.tick := io.tick; tx.io.code := io.code
    rx.io.in.valid := io.valid && !io.generate
    tx.io.in.valid := io.valid && io.generate
    for (in <- Seq(rx.io.in, tx.io.in)) {
        in.bits.data := Cat(io.hi, io.lo); in.bits.keep := io.keep
        in.bits.last := io.last; in.bits.error := io.error
    }
    io.ready := Mux(io.generate, tx.io.in.ready, rx.io.in.ready)
    rx.io.event.ready := io.eventReady
    io.eventValid := rx.io.event.valid
    val e = rx.io.event.bits
    io.routerIp := e.routerIp; io.sourceIp := e.sourceIp; io.destinationIp := e.destinationIp
    io.protocol := e.protocol; io.sourcePort := e.sourcePort; io.destinationPort := e.destinationPort
    io.sequence := e.sequence; io.totalLength := e.totalLength; io.identification := e.identification
    io.dontFragment := e.dontFragment; io.nextHopMtu := e.nextHopMtu
    io.icmpType := e.icmpType; io.icmpCode := e.icmpCode; io.pointer := e.pointer
    tx.io.out.ready := io.outReady
    io.outValid := tx.io.out.valid; io.outLo := tx.io.out.bits.data(63, 0)
    io.outHi := tx.io.out.bits.data(127, 64); io.outKeep := tx.io.out.bits.keep
    io.outLast := tx.io.out.bits.last; io.outDestination := tx.io.destinationIp
    io.rxDropped := rx.io.dropped; io.txDropped := tx.io.dropped; io.txSuppressed := tx.io.suppressed
}

/** Focused emitter for isolated development; the main Emit also emits this top. */
object EmitIcmpErrors extends App {
    require(args.length == 1)
    val out = Paths.get(args(0)); Files.createDirectories(out)
    Files.writeString(out.resolve("IcmpErrorsHarness.fir"), ChiselStage.emitCHIRRTL(new IcmpErrorsHarness))
    ChiselStage.emitSystemVerilogFile(new IcmpErrorsHarness, Array("--target-dir", out.toString))
}
