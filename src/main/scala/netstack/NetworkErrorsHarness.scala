package netstack

import chisel3._
import chisel3.util._
import _root_.circt.stage.ChiselStage
import java.nio.file.{Files, Paths}

/** Small geometries expose timer expiry/history wrap without altering production defaults. */
class NetworkErrorsHarness extends Module {
    val io = IO(new Bundle {
        val errorValid = Input(Bool()); val error = Input(new IcmpErrorEvent)
        val tcpTuple = Input(new TcpPathTuple)
        val tcpState = Input(UInt(4.W)); val tcpGeneration = Input(UInt(32.W))
        val tcpOldest = Input(UInt(32.W)); val tcpActual = Input(UInt(32.W)); val tcpReserved = Input(UInt(32.W))
        val tcpMtu = Input(UInt(16.W)); val tcpLimit = Input(UInt(16.W))
        val tcpTick = Input(Bool()); val tcpClear = Input(Bool())
        val updateValid = Output(Bool()); val updateMtu = Output(UInt(16.W))
        val updateGeneration = Output(UInt(32.W)); val updateIncrease = Output(Bool())
        val tcpNoticeValid = Output(Bool()); val tcpNotice = Output(new TcpNetworkError)
        val commandValid = Input(Bool()); val commandReady = Output(Bool())
        val command = Input(new UdpSendCommand)
        val inValid = Input(Bool()); val inReady = Output(Bool())
        val inLo = Input(UInt(64.W)); val inHi = Input(UInt(64.W))
        val inKeep = Input(UInt(16.W)); val inLast = Input(Bool()); val inError = Input(Bool())
        val outReady = Input(Bool()); val outValid = Output(Bool())
        val outLo = Output(UInt(64.W)); val outHi = Output(UInt(64.W))
        val outKeep = Output(UInt(16.W)); val outLast = Output(Bool())
        val outDestination = Output(UInt(32.W))
        val udpMtu = Input(UInt(16.W)); val udpTick = Input(Bool()); val udpFlush = Input(Bool())
        val routedValid = Input(Bool()); val routedSuccess = Input(Bool())
        val udpDropped = Output(Bool()); val udpNoticeValid = Output(Bool())
        val udpNotice = Output(new UdpPathNotice)
    })
    val tcp = Module(new TcpIcmpFeedback(1, 300))
    tcp.io.error.valid := io.errorValid; tcp.io.error.bits := io.error
    tcp.io.tuple := io.tcpTuple; tcp.io.state := io.tcpState; tcp.io.generation := io.tcpGeneration
    tcp.io.oldest := io.tcpOldest; tcp.io.actual := io.tcpActual; tcp.io.reserved := io.tcpReserved
    tcp.io.pathMtu := io.tcpMtu; tcp.io.limit := io.tcpLimit
    tcp.io.tick := io.tcpTick; tcp.io.clear := io.tcpClear
    io.updateValid := tcp.io.update.valid; io.updateMtu := tcp.io.update.bits.mtu
    io.updateGeneration := tcp.io.update.bits.generation; io.updateIncrease := tcp.io.update.bits.allowIncrease
    io.tcpNoticeValid := tcp.io.notice.valid; io.tcpNotice := tcp.io.notice.bits
    val udp = Module(new Ipv4UdpPathTransmit(128, 1, entries = 2, historyDepth = 2))
    udp.io.error.valid := io.errorValid; udp.io.error.bits := io.error
    udp.io.command.valid := io.commandValid; udp.io.command.bits := io.command
    io.commandReady := udp.io.command.ready
    udp.io.in.valid := io.inValid; udp.io.in.bits.data := Cat(io.inHi, io.inLo)
    udp.io.in.bits.keep := io.inKeep; udp.io.in.bits.last := io.inLast; udp.io.in.bits.error := io.inError
    io.inReady := udp.io.in.ready
    udp.io.out.ready := io.outReady; io.outValid := udp.io.out.valid
    io.outLo := udp.io.out.bits.data(63, 0); io.outHi := udp.io.out.bits.data(127, 64)
    io.outKeep := udp.io.out.bits.keep; io.outLast := udp.io.out.bits.last
    io.outDestination := udp.io.destinationIp
    udp.io.mtu := io.udpMtu; udp.io.tick := io.udpTick; udp.io.flush := io.udpFlush
    udp.io.routed.valid := io.routedValid; udp.io.routed.bits := io.routedSuccess
    io.udpDropped := udp.io.dropped; io.udpNoticeValid := udp.io.notice.valid; io.udpNotice := udp.io.notice.bits
}

object EmitNetworkErrors extends App {
    require(args.length == 1)
    val out = Paths.get(args(0)); Files.createDirectories(out)
    Files.writeString(out.resolve("NetworkErrorsHarness.fir"), ChiselStage.emitCHIRRTL(new NetworkErrorsHarness))
    ChiselStage.emitSystemVerilogFile(new NetworkErrorsHarness, Array("--target-dir", out.toString))
}
