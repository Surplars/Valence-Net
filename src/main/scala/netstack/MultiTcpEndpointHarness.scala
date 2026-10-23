package netstack

import chisel3._
import chisel3.util._

/** Four-context test adapter; production interfaces remain explicit per-port. */
class MultiTcpEndpointHarness(val ethernet: Boolean = false) extends Module {
    val io = IO(new Bundle {
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
        val freeBanks = Output(UInt(3.W)); val connectionOutstanding = Output(UInt(3.W))
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
        val outstanding = Output(UInt(5.W)); val fault = Output(Bool())
        val netValid = Input(Bool()); val netReady = Output(Bool())
        val netLo = Input(UInt(64.W)); val netHi = Input(UInt(64.W)); val netKeep = Input(UInt(16.W))
        val netLast = Input(Bool()); val netError = Input(Bool())
        val rxDrops = Output(UInt(5.W)); val rxBusyDropped = Output(Bool())
        val txTcp = Output(Bool()); val routedValid = Output(Bool()); val routedFailed = Output(Bool())
        val routedTag = Output(UInt(32.W)); val routedGeneration = Output(UInt(32.W))
        val udpReady = Input(Bool()); val udpValid = Output(Bool()); val udpLo = Output(UInt(64.W))
        val udpHi = Output(UInt(64.W)); val udpKeep = Output(UInt(16.W)); val udpLast = Output(Bool())
    })
    val apps = Wire(Vec(4, new MultiTcpApplicationPort(4, 4)))
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
        val dut = Module(new MultiEthernetTcpUdpEndpoint(4, 128, 4, 128, 128, 4, 10, 24, 4, true))
        dut.io.localIp := "h0a000001".U; dut.io.localMac := "h020000000001".U
        dut.io.netmask := "hffffff00".U; dut.io.gateway := 0.U; dut.io.mtu := 1500.U
        dut.io.tick := io.tick; dut.io.flushArp := false.B
        dut.io.rx <> rx; tx <> dut.io.tx; apps <> dut.io.connections
        dut.io.completed := completed; packet := dut.io.packet
        io.packetConnection := dut.io.packetConnection; io.txTcp := dut.io.txTcp
        io.routedValid := dut.io.routed.valid; io.routedFailed := dut.io.routed.bits.failed
        io.routedTag := dut.io.routed.bits.tag; io.routedGeneration := dut.io.routed.bits.generation
        dut.io.udpSend.valid := false.B; dut.io.udpSend.bits := 0.U.asTypeOf(new UdpSendCommand)
        dut.io.udpIn.valid := false.B; dut.io.udpIn.bits := 0.U.asTypeOf(new PacketBeat(16))
        dut.io.udpOut.ready := io.udpReady
        io.udpValid := dut.io.udpOut.valid; io.udpLo := dut.io.udpOut.bits.data(63, 0)
        io.udpHi := dut.io.udpOut.bits.data(127, 64); io.udpKeep := dut.io.udpOut.bits.keep
        io.udpLast := dut.io.udpOut.bits.last
        io.completionAccepted := dut.io.completionAccepted; io.invalidCompletion := dut.io.invalidCompletion
        io.outstanding := dut.io.outstanding; io.fault := dut.io.fault || dut.io.conflict
        io.rxDrops := dut.io.rxDrops; io.rxBusyDropped := dut.io.rxBusyDropped
    } else {
        val dut = Module(new MultiTcpIpv4Endpoint(4, 4, 128, 128, 128, 10, 24, 4, 3, 4, true))
        dut.io.localIp := "h0a000001".U; dut.io.tick := io.tick
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
