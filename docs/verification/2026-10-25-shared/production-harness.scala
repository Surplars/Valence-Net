package netstack

import chisel3._
import chisel3.util._

/** Four-context test adapter; production interfaces remain explicit per-port. */
class MultiTcpEndpointHarness(val ethernet: Boolean = false, val udpPorts: Seq[Int] = Seq.empty) extends Module {
    val io = IO(new Bundle {
        val exceptionReady = Input(Bool()); val exceptionValid = Output(Bool())
        val exceptionLo = Output(UInt(64.W)); val exceptionHi = Output(UInt(64.W))
        val exceptionKeep = Output(UInt(16.W)); val exceptionLast = Output(Bool())
        val exceptionDestination = Output(UInt(32.W))
        val pathMtu = Output(UInt(16.W)); val sendMss = Output(UInt(16.W))
        val networkErrorValid = Output(Bool()); val networkErrorType = Output(UInt(8.W))
        val networkErrorCode = Output(UInt(8.W)); val networkErrorMtu = Output(UInt(16.W))
        val udpCommandValid = Input(Bool()); val udpCommandReady = Output(Bool())
        val udpTxDestination = Input(UInt(32.W)); val udpTxSourcePort = Input(UInt(16.W))
        val udpTxDestinationPort = Input(UInt(16.W)); val udpTxLength = Input(UInt(16.W))
        val udpTxIdentification = Input(UInt(16.W))
        val udpDataValid = Input(Bool()); val udpDataReady = Output(Bool())
        val udpDataLo = Input(UInt(64.W)); val udpDataHi = Input(UInt(64.W))
        val udpDataKeep = Input(UInt(16.W)); val udpDataLast = Input(Bool())
        val udpNoticeValid = Output(Bool()); val udpNoticeLocal = Output(Bool())
        val udpNoticeMtu = Output(UInt(16.W)); val udpNoticeType = Output(UInt(8.W)); val udpNoticeCode = Output(UInt(8.W))
        val appConnection = Input(UInt(2.W)); val readConnection = Input(UInt(2.W))
        val openMask = Input(UInt(4.W)); val openReady = Output(UInt(4.W)); val openConflict = Output(UInt(4.W))
        val passive = Input(Bool()); val remoteIp = Input(UInt(32.W))
        val localPort = Input(UInt(16.W)); val remotePort = Input(UInt(16.W))
        val initialSequence = Input(UInt(32.W)); val localMss = Input(UInt(16.W))
        val closeMask = Input(UInt(4.W)); val closeReady = Output(UInt(4.W)); val abort = Input(Bool())
        val noticeReadyMask = Input(UInt(4.W)); val noticeValid = Output(UInt(4.W))
        val congestionWindow = Input(UInt(32.W)); val tick = Input(Bool())
        val states = Output(UInt(16.W)); val generation = Output(UInt(32.W))
        val sendOldest = Output(UInt(32.W)); val sendNext = Output(UInt(32.W))
        val sendActual = Output(UInt(32.W)); val receiveNext = Output(UInt(32.W))
        val receiveWindow = Output(UInt(32.W)); val retries = Output(UInt(8.W))
        val freeBanks = Output(UInt(4.W)); val connectionOutstanding = Output(UInt(5.W))
        val reorderCount = Output(UInt(4.W)); val timerRunning = Output(Bool()); val rto = Output(UInt(32.W))
        val allocateValid = Input(Bool()); val allocateReady = Output(Bool())
        val length = Input(UInt(16.W)); val context = Input(UInt(64.W))
        val valid = Input(Bool()); val ready = Output(Bool())
        val lo = Input(UInt(64.W)); val hi = Input(UInt(64.W)); val keep = Input(UInt(16.W))
        val last = Input(Bool()); val error = Input(Bool())
        val storedReadyMask = Input(UInt(4.W)); val storedValid = Output(UInt(4.W)); val storedError = Output(UInt(4.W))
        val receivedReady = Input(Bool()); val receivedValid = Output(Bool())
        val receivedLo = Output(UInt(64.W)); val receivedHi = Output(UInt(64.W)); val receivedKeep = Output(UInt(16.W))
        val receivedLast = Output(Bool())
        val outReady = Input(Bool()); val outValid = Output(Bool())
        val outLo = Output(UInt(64.W)); val outHi = Output(UInt(64.W)); val outKeep = Output(UInt(16.W))
        val outLast = Output(Bool()); val outError = Output(Bool())
        val packetTag = Output(UInt(32.W)); val packetGeneration = Output(UInt(32.W))
        val packetConnection = Output(UInt(2.W)); val packetEnd = Output(UInt(32.W))
        val packetRetry = Output(Bool()); val packetData = Output(Bool()); val packetConsumes = Output(Bool())
        val packetDestination = Output(UInt(32.W)); val packetContext = Output(UInt(64.W))
        val completedValid = Input(Bool()); val completedTag = Input(UInt(32.W))
        val completedGeneration = Input(UInt(32.W)); val completedFailed = Input(Bool())
        val completionAccepted = Output(Bool()); val invalidCompletion = Output(Bool())
        val outstanding = Output(UInt(7.W)); val fault = Output(Bool())
        val netValid = Input(Bool()); val netReady = Output(Bool())
        val netLo = Input(UInt(64.W)); val netHi = Input(UInt(64.W)); val netKeep = Input(UInt(16.W))
        val netLast = Input(Bool()); val netError = Input(Bool())
        val rxDrops = Output(UInt(5.W)); val rxBusyDropped = Output(Bool())
        val txTcp = Output(Bool()); val routedValid = Output(Bool()); val routedFailed = Output(Bool())
        val routedTag = Output(UInt(32.W)); val routedGeneration = Output(UInt(32.W))
        val udpReady = Input(Bool()); val udpValid = Output(Bool()); val udpLo = Output(UInt(64.W))
        val udpHi = Output(UInt(64.W)); val udpKeep = Output(UInt(16.W)); val udpLast = Output(Bool())
    })
    val apps = Wire(Vec(4, new MultiTcpApplicationPort(8, 16)))
    val rx = Wire(Decoupled(new PacketBeat(16)))
    val tx = Wire(Decoupled(new PacketBeat(16)))
    val packet = Wire(new TcpWireDescriptor)
    val completed = Wire(Valid(new TcpWireResult))
    completed.valid := io.completedValid; completed.bits.tag := io.completedTag
    completed.bits.generation := io.completedGeneration; completed.bits.failed := io.completedFailed
    rx.valid := io.netValid; io.netReady := rx.ready
    rx.bits.data := Cat(io.netHi, io.netLo); rx.bits.keep := io.netKeep
    rx.bits.last := io.netLast; rx.bits.error := io.netError
    tx.ready := io.outReady; io.outValid := tx.valid
    io.outLo := tx.bits.data(63, 0); io.outHi := tx.bits.data(127, 64)
    io.outKeep := tx.bits.keep; io.outLast := tx.bits.last; io.outError := tx.bits.error

    if (ethernet) {
        io.exceptionValid := false.B; io.exceptionLo := 0.U; io.exceptionHi := 0.U
        io.exceptionKeep := 0.U; io.exceptionLast := false.B; io.exceptionDestination := 0.U
        val dut = Module(new MultiEthernetTcpUdpEndpoint(4, 128, 8, 128, 4096, 16, 10, 24, 16, true, udpPorts))
        dut.io.localIp := "h0a000001".U; dut.io.localMac := "h020000000001".U
        dut.io.netmask := "hffffff00".U; dut.io.gateway := 0.U; dut.io.mtu := 1500.U
        dut.io.tick := io.tick; dut.io.flushArp := false.B
        dut.io.rx <> rx; tx <> dut.io.tx; apps <> dut.io.connections
        dut.io.completed := completed; packet := dut.io.packet
        io.packetConnection := dut.io.packetConnection; io.txTcp := dut.io.txTcp
        io.routedValid := dut.io.routed.valid; io.routedFailed := dut.io.routed.bits.failed
        io.routedTag := dut.io.routed.bits.tag; io.routedGeneration := dut.io.routed.bits.generation
        dut.io.udpSend.valid := io.udpCommandValid; io.udpCommandReady := dut.io.udpSend.ready
        dut.io.udpSend.bits := 0.U.asTypeOf(new UdpSendCommand)
        dut.io.udpSend.bits.sourceIp := "h0a000001".U
        dut.io.udpSend.bits.destinationIp := io.udpTxDestination
        dut.io.udpSend.bits.sourcePort := io.udpTxSourcePort; dut.io.udpSend.bits.destinationPort := io.udpTxDestinationPort
        dut.io.udpSend.bits.length := io.udpTxLength; dut.io.udpSend.bits.identification := io.udpTxIdentification
        dut.io.udpSend.bits.ttl := 64.U
        dut.io.udpIn.valid := io.udpDataValid; io.udpDataReady := dut.io.udpIn.ready
        dut.io.udpIn.bits.data := Cat(io.udpDataHi, io.udpDataLo); dut.io.udpIn.bits.keep := io.udpDataKeep
        dut.io.udpIn.bits.last := io.udpDataLast; dut.io.udpIn.bits.error := false.B
        io.udpNoticeValid := dut.io.udpNetworkError.valid; io.udpNoticeLocal := dut.io.udpNetworkError.bits.local
        io.udpNoticeMtu := dut.io.udpNetworkError.bits.mtu
        io.udpNoticeType := dut.io.udpNetworkError.bits.icmpType; io.udpNoticeCode := dut.io.udpNetworkError.bits.icmpCode
        dut.io.udpOut.ready := io.udpReady
        io.udpValid := dut.io.udpOut.valid; io.udpLo := dut.io.udpOut.bits.data(63, 0)
        io.udpHi := dut.io.udpOut.bits.data(127, 64); io.udpKeep := dut.io.udpOut.bits.keep
        io.udpLast := dut.io.udpOut.bits.last
        io.completionAccepted := dut.io.completionAccepted; io.invalidCompletion := dut.io.invalidCompletion
        io.outstanding := dut.io.outstanding; io.fault := dut.io.fault || dut.io.conflict
        io.rxDrops := dut.io.rxDrops; io.rxBusyDropped := dut.io.rxBusyDropped
    } else {
        io.udpCommandReady := false.B; io.udpDataReady := false.B
        io.udpNoticeValid := false.B; io.udpNoticeLocal := false.B; io.udpNoticeMtu := 0.U
        io.udpNoticeType := 0.U; io.udpNoticeCode := 0.U
        val dut = Module(new MultiTcpIpv4Endpoint(4, 4, 128, 128, 128, 10, 24, 4, 3, 4, true))
        dut.io.localIp := "h0a000001".U; dut.io.tick := io.tick
        dut.io.netmask := "hffffff00".U
        dut.io.pathMtuLimit := 65535.U
        dut.io.icmpError.valid := false.B; dut.io.icmpError.bits := 0.U.asTypeOf(new IcmpErrorEvent)
        dut.io.exceptionOut.ready := io.exceptionReady
        io.exceptionValid := dut.io.exceptionOut.valid
        io.exceptionLo := dut.io.exceptionOut.bits.data(63, 0); io.exceptionHi := dut.io.exceptionOut.bits.data(127, 64)
        io.exceptionKeep := dut.io.exceptionOut.bits.keep; io.exceptionLast := dut.io.exceptionOut.bits.last
        io.exceptionDestination := dut.io.exceptionDestinationIp
        dut.io.rx <> rx; tx <> dut.io.out; apps <> dut.io.connections
        dut.io.completed := completed; packet := dut.io.packet
        io.packetConnection := dut.io.packetConnection; io.txTcp := true.B
        io.routedValid := false.B; io.routedFailed := false.B; io.routedTag := 0.U; io.routedGeneration := 0.U
        io.udpValid := false.B; io.udpLo := 0.U; io.udpHi := 0.U; io.udpKeep := 0.U; io.udpLast := false.B
        io.completionAccepted := dut.io.completionAccepted; io.invalidCompletion := dut.io.invalidCompletion
        io.outstanding := dut.io.outstanding; io.fault := dut.io.fault
        io.rxDrops := dut.io.rxDrops; io.rxBusyDropped := dut.io.rxBusyDropped
    }
    io.packetTag := packet.tag; io.packetGeneration := packet.completion.generation
    io.packetEnd := packet.completion.endSequence; io.packetRetry := packet.completion.retransmission
    io.packetData := packet.data; io.packetConsumes := packet.sequenceSpace
    io.packetDestination := packet.destinationIp; io.packetContext := packet.context
    for (i <- 0 until 4) {
        val app = apps(i)
        app.open.valid := io.openMask(i)
        app.open.bits.passive := io.passive
        app.open.bits.localIp := "h0a000001".U; app.open.bits.remoteIp := io.remoteIp
        app.open.bits.localPort := io.localPort; app.open.bits.remotePort := io.remotePort
        app.open.bits.initialSequence := io.initialSequence; app.open.bits.localMss := io.localMss
        app.open.bits.offerScale := false.B; app.open.bits.localScale := 0.U
        val isn = RegEnable(io.initialSequence, app.open.fire)
        app.passiveInitialSequence := isn
        app.close.valid := io.closeMask(i); app.close.bits := io.abort
        app.notice.ready := io.noticeReadyMask(i); app.congestionWindow := io.congestionWindow
        app.allocate.valid := io.allocateValid && io.appConnection === i.U
        app.allocate.bits.length := io.length; app.allocate.bits.context := io.context
        app.in.valid := io.valid && io.appConnection === i.U
        app.in.bits.data := Cat(io.hi, io.lo); app.in.bits.keep := io.keep
        app.in.bits.last := io.last; app.in.bits.error := io.error
        app.stored.ready := io.storedReadyMask(i)
        app.received.ready := io.receivedReady && io.readConnection === i.U
    }
    val app = apps(io.appConnection); val read = apps(io.readConnection)
    io.openReady := VecInit(apps.map(_.open.ready)).asUInt
    io.openConflict := VecInit(apps.map(_.openConflict)).asUInt
    io.closeReady := VecInit(apps.map(_.close.ready)).asUInt
    io.noticeValid := VecInit(apps.map(_.notice.valid)).asUInt
    io.states := VecInit(apps.map(_.state)).asUInt
    io.pathMtu := app.pathMtu; io.sendMss := app.sendMss
    io.networkErrorValid := app.networkError.valid
    io.networkErrorType := app.networkError.bits.icmpType; io.networkErrorCode := app.networkError.bits.icmpCode
    io.networkErrorMtu := app.networkError.bits.mtu
    io.generation := app.generation; io.sendOldest := app.sendOldest
    io.sendNext := app.sendNext; io.sendActual := app.sendActual; io.receiveNext := app.receiveNext
    io.receiveWindow := app.receiveWindow; io.retries := app.retries; io.freeBanks := app.freeBanks
    io.connectionOutstanding := app.outstanding; io.reorderCount := app.reorderCount
    io.timerRunning := app.timerRunning; io.rto := app.rto
    io.allocateReady := app.allocate.ready; io.ready := app.in.ready
    io.storedValid := VecInit(apps.map(_.stored.valid)).asUInt
    io.storedError := VecInit(apps.map(_.stored.bits.error)).asUInt
    io.receivedValid := read.received.valid
    io.receivedLo := read.received.bits.data(63, 0); io.receivedHi := read.received.bits.data(127, 64)
    io.receivedKeep := read.received.bits.keep; io.receivedLast := read.received.bits.last
}

class MultiEthernetTcpHarness extends MultiTcpEndpointHarness(true)

class MultiEthernetPortHarness extends MultiTcpEndpointHarness(true, Seq(1300))
