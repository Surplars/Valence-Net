package iperf

import chisel3._
import chisel3.util._
import netstack._
import soc.ip.ethernet._
import soc.ip.bus._

object Address {
    val ip = BigInt("c0a8890a", 16) // 192.168.137.10
    val gateway = BigInt("c0a88901", 16) // PC: 192.168.137.1
    val mac = BigInt("020000000080", 16)
}
class IperfStack(val txBanks: Int = 64) extends Module {
    val io = IO(new Bundle {
        val rxFrame = Flipped(Decoupled(new EthernetFrameBeat(4)))
        val link = Input(Bool()); val tick = Input(Bool())
        val txData = Output(UInt(8.W)); val txEnable = Output(Bool()); val txError = Output(Bool())
        val phase = Output(UInt(5.W)); val reverse = Output(Bool())
        val rxBytes = Output(UInt(64.W)); val txBytes = Output(UInt(64.W))
        val errors = Output(UInt(32.W)); val fault = Output(Bool())
        // Per-cycle event multiplicities: RX drops, RX busy, TX drops, physical TX, rejected TX.
        val debugEvents = Output(Vec(5, UInt(8.W)))
    })
    val net = Module(new MultiEthernetTcpUdpEndpoint(connectionCount = 2, txBanks = txBanks, rxWords = 16384, pressureWindowUpdates = true))
    val app = Module(new Iperf3Application(txBanks = txBanks))
    val rx = Module(new MacPack)
    val tx = Module(new MacTransmit)
    net.io.localIp := Address.ip.U; net.io.localMac := Address.mac.U
    net.io.netmask := "hffffff00".U; net.io.gateway := Address.gateway.U; net.io.mtu := 1500.U
    net.io.tick := io.tick; net.io.flushArp := false.B
    rx.io.in <> io.rxFrame; net.io.rx <> rx.io.out
    tx.io.in <> net.io.tx; tx.io.tcp := net.io.txTcp
    tx.io.tag := net.io.txTag; tx.io.generation := net.io.txGeneration
    tx.io.routed <> net.io.routed; net.io.completed <> tx.io.completed
    tx.io.link := io.link
    io.txData := tx.io.data; io.txEnable := tx.io.enable; io.txError := tx.io.error
    app.io.connections <> net.io.connections
    app.io.localIp := Address.ip.U; app.io.tick := io.tick; app.io.enabled := io.link
    net.io.udpSend.valid := false.B; net.io.udpSend.bits := 0.U.asTypeOf(net.io.udpSend.bits)
    net.io.udpIn.valid := false.B; net.io.udpIn.bits := 0.U.asTypeOf(net.io.udpIn.bits)
    net.io.udpOut.ready := true.B
    io.phase := app.io.phase; io.reverse := app.io.reverse
    io.rxBytes := app.io.rxBytes; io.txBytes := app.io.txBytes; io.errors := app.io.errors
    val fault = RegInit(false.B)
    when(net.io.invalidCompletion || net.io.fault) { fault := true.B }
    io.fault := fault
    io.debugEvents(0) := net.io.rxDrops
    io.debugEvents(1) := net.io.rxBusyDropped
    io.debugEvents(2) := net.io.txDrops
    io.debugEvents(3) := tx.io.physicalDone
    io.debugEvents(4) := tx.io.rejected
}
class IperfGmiiHarness(val txBanks: Int = 64) extends Module {
    val io = IO(new Bundle {
        val rxData = Input(UInt(8.W)); val rxValid = Input(Bool()); val rxError = Input(Bool())
        val link = Input(Bool()); val tick = Input(Bool())
        val txData = Output(UInt(8.W)); val txEnable = Output(Bool()); val txError = Output(Bool())
        val phase = Output(UInt(5.W)); val reverse = Output(Bool())
        val rxBytes = Output(UInt(64.W)); val txBytes = Output(UInt(64.W))
        val errors = Output(UInt(32.W)); val fault = Output(Bool())
        val rxDropped = Output(Bool())
    })
    val stack = Module(new IperfStack(txBanks))
    val mac = Module(new GmiiFrameRx(frameSlots = 8))
    mac.io.gmiiData := io.rxData; mac.io.gmiiValid := io.rxValid; mac.io.gmiiError := io.rxError
    mac.io.enable := io.link; mac.io.promiscuous := false.B
    mac.io.broadcastEnable := true.B; mac.io.macAddress := Address.mac.U
    stack.io.rxFrame <> mac.io.frame
    stack.io.link := io.link; stack.io.tick := io.tick
    io.txData := stack.io.txData; io.txEnable := stack.io.txEnable; io.txError := stack.io.txError
    io.phase := stack.io.phase; io.reverse := stack.io.reverse
    io.rxBytes := stack.io.rxBytes; io.txBytes := stack.io.txBytes
    io.errors := stack.io.errors; io.fault := stack.io.fault
    io.rxDropped := mac.io.dropped
}

/** Recovered RX domain is independent of the continuously running TX/core.
  * Common reset coordinates packet ownership and the asynchronous FIFO.
  */
class IperfBoardCore extends RawModule {
    val txClock = IO(Input(Clock())); val rxClock = IO(Input(Clock()))
    val commonReset = IO(Input(AsyncReset()))
    val rxClockLocked = IO(Input(Bool()))
    val rxData = IO(Input(UInt(8.W))); val rxValid = IO(Input(Bool())); val rxError = IO(Input(Bool()))
    val txData = IO(Output(UInt(8.W))); val txEnable = IO(Output(Bool())); val txError = IO(Output(Bool()))
    val mdioIn = IO(Input(Bool())); val mdioOut = IO(Output(Bool())); val mdioOe = IO(Output(Bool()))
    val mdc = IO(Output(Bool())); val linkUp = IO(Output(Bool()))
    val status = IO(Output(UInt(8.W)))
    // Read-only diagnostic taps; UART has no backpressure connection to networking.
    val debugPhase = IO(Output(UInt(5.W)))
    val debugReverse = IO(Output(Bool()))
    val debugRxBytes = IO(Output(UInt(64.W)))
    val debugTxBytes = IO(Output(UInt(64.W)))
    val debugErrors = IO(Output(UInt(32.W)))
    val debugPhyFault = IO(Output(UInt(8.W)))
    val debugDiagnostics = IO(Output(UInt(608.W)))
    val rxInbandLink = IO(Input(Bool()))
    val release = Module(new CdcResetRelease)
    release.clockIn := txClock; release.asyncReset := commonReset
    // RX lock must never hold the PHY manager in reset: the PHY can stop RXC
    // until reset release / negotiation. Only the complete packet datapath resets.
    val networkReset = (commonReset.asBool || !rxClockLocked).asAsyncReset
    val networkRelease = Module(new CdcResetRelease)
    networkRelease.clockIn := txClock; networkRelease.asyncReset := networkReset
    val rxRelease = Module(new CdcResetRelease)
    rxRelease.clockIn := rxClock; rxRelease.asyncReset := networkReset
    val crossing = Module(new EthernetFrameClockBridge(64))
    crossing.sourceClock := rxClock; crossing.destinationClock := txClock; crossing.commonReset := networkReset
    val link = Wire(Bool())
    val rxLink = Module(new CdcLevel)
    rxLink.clockIn := rxClock; rxLink.resetIn := rxRelease.resetOut; rxLink.levelIn := link
    val rxDiagnostics = Module(new RxDiagnosticSnapshot)
    rxDiagnostics.sourceClock := rxClock; rxDiagnostics.destinationClock := txClock
    // Preserve diagnostics through RX-lock loss; only common cold reset starts a new epoch.
    rxDiagnostics.commonReset := commonReset
    rxDiagnostics.inbandLink := rxInbandLink
    withClockAndReset(rxClock, rxRelease.resetOut) {
        val mac = Module(new GmiiFrameRx(frameSlots = 8, diagnostics = true))
        mac.io.gmiiData := rxData; mac.io.gmiiValid := rxValid; mac.io.gmiiError := rxError
        mac.io.enable := rxLink.levelOut
        mac.io.promiscuous := false.B; mac.io.broadcastEnable := true.B; mac.io.macAddress := Address.mac.U
        crossing.source <> mac.io.frame
        rxDiagnostics.accepted := mac.io.accepted; rxDiagnostics.dropped := mac.io.dropped
        rxDiagnostics.badFcs := mac.io.badFcs; rxDiagnostics.dropReasons := mac.io.dropReasons.get
        rxDiagnostics.backpressured := mac.io.frame.valid && !mac.io.frame.ready
    }
    withClockAndReset(txClock, release.resetOut) {
        val phy = Module(new Rtl8211fPhyManager(clockHz = 125000000,
            startupCycles = 6250000, pollCycles = 12500000))
        val mdio = Module(new MdioClause22(125000000, 2500000))
        phy.io.command <> mdio.io.command; phy.io.response <> mdio.io.response
        phy.io.restart := false.B
        mdio.io.mdioIn := mdioIn; mdioOut := mdio.io.mdioOut; mdioOe := mdio.io.mdioOe; mdc := mdio.io.mdc
        // Register before crossing: no combinational rate/link decode at RX synchronizer D.
        link := RegNext(phy.io.linkUp && phy.io.requestedSpeed === EthernetSpeed.Mbps1000.U, false.B)
        linkUp := link
        val counter = RegInit(0.U(17.W))
        val tick = counter === 124999.U
        counter := Mux(tick, 0.U, counter + 1.U)
        val core = withClockAndReset(txClock, networkRelease.resetOut) { Module(new IperfStack) }
        core.io.rxFrame <> crossing.destination; core.io.link := link; core.io.tick := tick
        txData := core.io.txData; txEnable := core.io.txEnable; txError := core.io.txError
        status := Cat(core.io.fault, phy.io.fault.orR, link, core.io.phase)
        debugPhase := core.io.phase; debugReverse := core.io.reverse
        debugRxBytes := core.io.rxBytes; debugTxBytes := core.io.txBytes
        debugErrors := core.io.errors; debugPhyFault := phy.io.fault
        val stackCounts = RegInit(VecInit(Seq.fill(5)(0.U(32.W))))
        for (i <- 0 until 5) {
            when(core.io.debugEvents(i) =/= 0.U) { stackCounts(i) := stackCounts(i) + core.io.debugEvents(i) }
        }
        debugDiagnostics := Cat(rxDiagnostics.totals(447,416), stackCounts.asUInt, rxDiagnostics.totals(415,0))
    }
}
