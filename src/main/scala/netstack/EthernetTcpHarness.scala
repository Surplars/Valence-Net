package netstack

import chisel3._
import chisel3.util._

class EthernetTcpHarness(val txBanks: Int = 4, val autonomous: Boolean = false) extends Module {
    val io = IO(new Bundle {
        val openValid = Input(Bool())
        val passive = Input(Bool())
        val localIp = Input(UInt(32.W))
        val remoteIp = Input(UInt(32.W))
        val localPort = Input(UInt(16.W))
        val remotePort = Input(UInt(16.W))
        val initialSequence = Input(UInt(32.W))
        val localMss = Input(UInt(16.W))
        val offerScale = Input(Bool())
        val localScale = Input(UInt(4.W))
        val passiveInitialSequence = Input(UInt(32.W))
        val closeValid = Input(Bool())
        val abort = Input(Bool())
        val noticeReady = Input(Bool())
        val congestionWindow = Input(UInt(32.W))
        val tick = Input(Bool())
        val openReady = Output(Bool())
        val closeReady = Output(Bool())
        val state = Output(UInt(4.W))
        val generation = Output(UInt(32.W))
        val sendOldest = Output(UInt(32.W))
        val sendNext = Output(UInt(32.W))
        val sendActual = Output(UInt(32.W))
        val receiveNext = Output(UInt(32.W))
        val sendMss = Output(UInt(16.W))
        val noticeValid = Output(Bool())
        val noticeGeneration = Output(UInt(32.W))
        val noticeFlags = Output(UInt(5.W))
        val timeoutValid = Output(Bool())
        val timeoutGeneration = Output(UInt(32.W))
        val timerRunning = Output(Bool())
        val timerExpired = Output(Bool())
        val retries = Output(UInt(8.W))
        val allocateValid = Input(Bool())
        val allocateReady = Output(Bool())
        val length = Input(UInt(16.W))
        val context = Input(UInt(64.W))
        val valid = Input(Bool())
        val ready = Output(Bool())
        val lo = Input(UInt(64.W))
        val hi = Input(UInt(64.W))
        val keep = Input(UInt(16.W))
        val last = Input(Bool())
        val error = Input(Bool())
        val storedReady = Input(Bool())
        val storedValid = Output(Bool())
        val storedError = Output(Bool())
        val storedContext = Output(UInt(64.W))
        val outReady = Input(Bool())
        val outValid = Output(Bool())
        val outLo = Output(UInt(64.W))
        val outHi = Output(UInt(64.W))
        val outKeep = Output(UInt(16.W))
        val outLast = Output(Bool())
        val outError = Output(Bool())
        val packetTag = Output(UInt(32.W))
        val packetGeneration = Output(UInt(32.W))
        val packetEnd = Output(UInt(32.W))
        val packetRetry = Output(Bool())
        val packetData = Output(Bool())
        val packetConsumes = Output(Bool())
        val packetDestination = Output(UInt(32.W))
        val packetContext = Output(UInt(64.W))
        val completedValid = Input(Bool())
        val completedTag = Input(UInt(32.W))
        val completedGeneration = Input(UInt(32.W))
        val completedFailed = Input(Bool())
        val completionAccepted = Output(Bool())
        val invalidCompletion = Output(Bool())
        val fault = Output(Bool())
        val rto = Output(UInt(32.W))
        val freeBanks = Output(UInt(log2Ceil(txBanks + 1).W))
        val outstanding = Output(UInt(3.W))
        val netValid = Input(Bool()); val netReady = Output(Bool())
        val netLo = Input(UInt(64.W)); val netHi = Input(UInt(64.W)); val netKeep = Input(UInt(16.W))
        val netLast = Input(Bool()); val netError = Input(Bool())
        val receivedReady = Input(Bool()); val receivedValid = Output(Bool())
        val receivedLo = Output(UInt(64.W)); val receivedHi = Output(UInt(64.W)); val receivedKeep = Output(UInt(16.W))
        val receiveWindow = Output(UInt(32.W)); val rxDrops = Output(UInt(2.W))
        val udpSendValid = Input(Bool()); val udpSendReady = Output(Bool())
        val udpLength = Input(UInt(16.W)); val udpId = Input(UInt(16.W))
        val udpValid = Input(Bool()); val udpReady = Output(Bool())
        val udpLo = Input(UInt(64.W)); val udpHi = Input(UInt(64.W)); val udpKeep = Input(UInt(16.W)); val udpLast = Input(Bool())
        val udpReceivedReady = Input(Bool()); val udpReceivedValid = Output(Bool())
        val udpReceivedLo = Output(UInt(64.W)); val udpReceivedHi = Output(UInt(64.W)); val udpReceivedKeep = Output(UInt(16.W))
        val udpReceivedLast = Output(Bool()); val udpReceivedLength = Output(UInt(16.W))
        val udpReceivedSource = Output(UInt(32.W)); val udpReceivedPort = Output(UInt(16.W))
        val outTcp = Output(Bool())
        val routeValid = Output(Bool()); val routeTag = Output(UInt(32.W)); val routeGeneration = Output(UInt(32.W)); val routeFailed = Output(Bool())
        val flushArp = Input(Bool())
    })
    val dut = Module(new EthernetTcpUdpEndpoint(128, txBanks, 128, 64, 4, 10, 24, 4, autonomous))
    dut.io.tcpIn.bits.data := Cat(io.hi,io.lo)
    dut.io.open.valid := io.openValid
    dut.io.open.bits.passive := io.passive
    dut.io.open.bits.localIp := io.localIp
    dut.io.open.bits.remoteIp := io.remoteIp
    dut.io.open.bits.localPort := io.localPort
    dut.io.open.bits.remotePort := io.remotePort
    dut.io.open.bits.initialSequence := io.initialSequence
    dut.io.open.bits.localMss := io.localMss
    dut.io.open.bits.offerScale := io.offerScale
    dut.io.open.bits.localScale := io.localScale
    dut.io.passiveInitialSequence := io.passiveInitialSequence
    dut.io.close.valid := io.closeValid
    dut.io.close.bits := io.abort
    dut.io.notice.ready := io.noticeReady
    dut.io.congestionWindow := io.congestionWindow
    dut.io.tick := io.tick
    io.openReady := dut.io.open.ready
    io.closeReady := dut.io.close.ready
    io.state := dut.io.state
    io.generation := dut.io.generation
    io.sendOldest := dut.io.sendOldest
    io.sendNext := dut.io.sendNext
    io.sendActual := dut.io.sendActual
    io.receiveNext := dut.io.receiveNext
    io.sendMss := dut.io.sendMss
    io.noticeValid := dut.io.notice.valid
    io.noticeGeneration := dut.io.notice.bits.generation
    io.noticeFlags := dut.io.notice.bits.flags
    io.timeoutValid := dut.io.timeout.valid
    io.timeoutGeneration := dut.io.timeout.bits
    io.timerRunning := dut.io.timerRunning
    io.timerExpired := dut.io.timerExpired
    io.retries := dut.io.retries
    dut.io.tcpAllocate.valid := io.allocateValid
    io.allocateReady := dut.io.tcpAllocate.ready
    dut.io.tcpAllocate.bits.length := io.length
    dut.io.tcpAllocate.bits.context := io.context
    dut.io.tcpIn.valid := io.valid
    io.ready := dut.io.tcpIn.ready
    dut.io.tcpIn.bits.keep := io.keep
    dut.io.tcpIn.bits.last := io.last
    dut.io.tcpIn.bits.error := io.error
    dut.io.tcpStored.ready := io.storedReady
    io.storedValid := dut.io.tcpStored.valid
    io.storedError := dut.io.tcpStored.bits.error
    io.storedContext := dut.io.tcpStored.bits.context
    dut.io.tx.ready := io.outReady
    io.outValid := dut.io.tx.valid
    io.outLo := dut.io.tx.bits.data(63,0)
    io.outHi := dut.io.tx.bits.data(127,64)
    io.outKeep := dut.io.tx.bits.keep
    io.outLast := dut.io.tx.bits.last
    io.outError := dut.io.tx.bits.error
    io.packetTag := dut.io.packet.tag
    io.packetGeneration := dut.io.packet.completion.generation
    io.packetEnd := dut.io.packet.completion.endSequence
    io.packetRetry := dut.io.packet.completion.retransmission
    io.packetData := dut.io.packet.data
    io.packetConsumes := dut.io.packet.sequenceSpace
    io.packetDestination := dut.io.packet.destinationIp
    io.packetContext := dut.io.packet.context
    dut.io.completed.valid := io.completedValid
    dut.io.completed.bits.tag := io.completedTag
    dut.io.completed.bits.generation := io.completedGeneration
    dut.io.completed.bits.failed := io.completedFailed
    io.completionAccepted := dut.io.completionAccepted
    io.invalidCompletion := dut.io.invalidCompletion
    io.fault := dut.io.fault
    io.rto := dut.io.rto
    io.freeBanks := dut.io.freeBanks
    io.outstanding := dut.io.outstanding
    dut.io.localIp := io.localIp
    dut.io.rx.valid := io.netValid; io.netReady := dut.io.rx.ready
    dut.io.rx.bits.data := Cat(io.netHi, io.netLo); dut.io.rx.bits.keep := io.netKeep
    dut.io.rx.bits.last := io.netLast; dut.io.rx.bits.error := io.netError
    dut.io.tcpOut.ready := io.receivedReady; io.receivedValid := dut.io.tcpOut.valid
    io.receivedLo := dut.io.tcpOut.bits.data(63,0); io.receivedHi := dut.io.tcpOut.bits.data(127,64)
    io.receivedKeep := dut.io.tcpOut.bits.keep; io.receiveWindow := dut.io.receiveWindow; io.rxDrops := dut.io.rxDrops
    dut.io.localMac := "h020000000001".U
    dut.io.netmask := "hffffff00".U; dut.io.gateway := 0.U; dut.io.mtu := 1500.U
    dut.io.flushArp := io.flushArp
    dut.io.udpSend.valid := io.udpSendValid; io.udpSendReady := dut.io.udpSend.ready
    dut.io.udpSend.bits := 0.U.asTypeOf(new UdpSendCommand)
    dut.io.udpSend.bits.sourceIp := io.localIp; dut.io.udpSend.bits.destinationIp := "h0a000002".U
    dut.io.udpSend.bits.sourcePort := 10000.U; dut.io.udpSend.bits.destinationPort := 20000.U
    dut.io.udpSend.bits.length := io.udpLength; dut.io.udpSend.bits.identification := io.udpId; dut.io.udpSend.bits.ttl := 64.U
    dut.io.udpIn.valid := io.udpValid; io.udpReady := dut.io.udpIn.ready
    dut.io.udpIn.bits.data := Cat(io.udpHi, io.udpLo); dut.io.udpIn.bits.keep := io.udpKeep
    dut.io.udpIn.bits.last := io.udpLast; dut.io.udpIn.bits.error := false.B
    dut.io.udpOut.ready := io.udpReceivedReady; io.udpReceivedValid := dut.io.udpOut.valid
    io.udpReceivedLo := dut.io.udpOut.bits.data(63, 0); io.udpReceivedHi := dut.io.udpOut.bits.data(127, 64)
    io.udpReceivedKeep := dut.io.udpOut.bits.keep; io.udpReceivedLast := dut.io.udpOut.bits.last
    io.udpReceivedLength := dut.io.udpLength; io.udpReceivedSource := dut.io.udpSourceIp; io.udpReceivedPort := dut.io.udpSourcePort
    io.outTcp := dut.io.txTcp
    io.routeValid := dut.io.routed.valid; io.routeTag := dut.io.routed.bits.tag
    io.routeGeneration := dut.io.routed.bits.generation; io.routeFailed := dut.io.routed.bits.failed
}

class EthernetServiceHarness extends EthernetTcpHarness(8, true)
