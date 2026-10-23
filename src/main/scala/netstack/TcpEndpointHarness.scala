package netstack

import chisel3._
import chisel3.util._

class TcpEndpointHarness(val autonomous: Boolean = false) extends Module {
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
        val freeBanks = Output(UInt(3.W))
        val outstanding = Output(UInt(3.W))
        val netValid = Input(Bool()); val netReady = Output(Bool())
        val netLo = Input(UInt(64.W)); val netHi = Input(UInt(64.W)); val netKeep = Input(UInt(16.W))
        val netLast = Input(Bool()); val netError = Input(Bool())
        val receivedReady = Input(Bool()); val receivedValid = Output(Bool())
        val receivedLo = Output(UInt(64.W)); val receivedHi = Output(UInt(64.W)); val receivedKeep = Output(UInt(16.W))
        val receiveWindow = Output(UInt(32.W)); val rxDrops = Output(UInt(2.W))
        val reorderCount = Output(UInt(4.W)); val reorderDropped = Output(Bool())
        val congestionLimit = Output(UInt(32.W)); val congestionThreshold = Output(UInt(32.W)); val fastRecovery = Output(Bool())
    })
    val dut = Module(new TcpIpv4Endpoint(4, 128, 64, 128, 10, 24, 4, 3, 4, autonomous))
    dut.io.in.bits.data := Cat(io.hi,io.lo)
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
    dut.io.allocate.valid := io.allocateValid
    io.allocateReady := dut.io.allocate.ready
    dut.io.allocate.bits.length := io.length
    dut.io.allocate.bits.context := io.context
    dut.io.in.valid := io.valid
    io.ready := dut.io.in.ready
    dut.io.in.bits.keep := io.keep
    dut.io.in.bits.last := io.last
    dut.io.in.bits.error := io.error
    dut.io.stored.ready := io.storedReady
    io.storedValid := dut.io.stored.valid
    io.storedError := dut.io.stored.bits.error
    io.storedContext := dut.io.stored.bits.context
    dut.io.out.ready := io.outReady
    io.outValid := dut.io.out.valid
    io.outLo := dut.io.out.bits.data(63,0)
    io.outHi := dut.io.out.bits.data(127,64)
    io.outKeep := dut.io.out.bits.keep
    io.outLast := dut.io.out.bits.last
    io.outError := dut.io.out.bits.error
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
    dut.io.received.ready := io.receivedReady; io.receivedValid := dut.io.received.valid
    io.receivedLo := dut.io.received.bits.data(63,0); io.receivedHi := dut.io.received.bits.data(127,64)
    io.receivedKeep := dut.io.received.bits.keep; io.receiveWindow := dut.io.receiveWindow; io.rxDrops := dut.io.rxDrops
    io.reorderCount := dut.io.reorderCount; io.reorderDropped := dut.io.reorderDropped
    io.congestionLimit := dut.io.congestionLimit; io.congestionThreshold := dut.io.congestionThreshold; io.fastRecovery := dut.io.fastRecovery
}

class TcpCongestedEndpointHarness extends TcpEndpointHarness(true)
