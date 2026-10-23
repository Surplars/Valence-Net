package netstack

import chisel3._
import chisel3.util._

/** Ethernet-II ARP + IPv4 UDP + bounded independent TCP sessions. 128-bit internal streams.
  * MAC-facing data excludes preamble/FCS/IFG. Configuration is static while
  * active. An ingress MAC adapter must buffer/drop complete frames.
  *
  * txTcp/tag/generation identify TCP frames at the MAC boundary. routed reports
  * a submitted frame or a routing drop, once, including packets that consume no
  * sequence space. The adapter combines these with actual MAC completions and
  * returns completed in TCP submission order. Never loop routed into completed
  * across a buffered MAC without accounting for its physical transmit latency.
  */
class MultiEthernetTcpUdpEndpoint(val connectionCount: Int = 4, val packetBeats: Int = 128, val txBanks: Int = 8,
    val txBeats: Int = 128, val rxWords: Int = 4096, val arpEntries: Int = 16,
    val ticksPerSecond: Int = 1000, val timeWaitTicks: Int = 120000,
    val completionDepth: Int = 16, val autonomousCongestion: Boolean = true) extends Module {
    val connectionBits = math.max(1, log2Ceil(connectionCount))
    val globalDepth = connectionCount * completionDepth
    val io = IO(new Bundle {
        val localIp = Input(UInt(32.W)); val localMac = Input(UInt(48.W))
        val netmask = Input(UInt(32.W)); val gateway = Input(UInt(32.W)); val mtu = Input(UInt(16.W))
        val tick = Input(Bool()); val flushArp = Input(Bool())
        val rx = Flipped(Decoupled(new PacketBeat(16))); val tx = Decoupled(new PacketBeat(16))
        val txTcp = Output(Bool()); val txTag = Output(UInt(32.W)); val txGeneration = Output(UInt(32.W))
        val routed = Valid(new TcpWireResult); val completed = Flipped(Valid(new TcpWireResult))
        val connections = Vec(connectionCount, new MultiTcpApplicationPort(txBanks, completionDepth))
        val txConnection = Output(UInt(connectionBits.W))
        val packetConnection = Output(UInt(connectionBits.W))
        val udpSend = Flipped(Decoupled(new UdpSendCommand))
        val udpIn = Flipped(Decoupled(new PacketBeat(16))); val udpOut = Decoupled(new PacketBeat(16))
        val udpSourceIp = Output(UInt(32.W)); val udpDestinationIp = Output(UInt(32.W))
        val udpSourcePort = Output(UInt(16.W)); val udpDestinationPort = Output(UInt(16.W))
        val udpLength = Output(UInt(16.W))
        val packet = Output(new TcpWireDescriptor)
        val completionAccepted = Output(Bool())
        val outstanding = Output(UInt(log2Ceil(globalDepth + 1).W))
        val rxBusyDropped = Output(Bool())
        val invalidCompletion = Output(Bool()); val fault = Output(Bool()); val conflict = Output(Bool())
        val rxDrops = Output(UInt(log2Ceil(2 * connectionCount + 10).W)); val txDrops = Output(UInt(2.W))
    })
    val ethernet = Module(new EthernetDispatch)
    val ethRx = Module(new EthernetIpv4Receive(packetBeats))
    val udpRx = Module(new Ipv4UdpReceive(packetBeats))
    val udpTx = Module(new Ipv4UdpTransmit(packetBeats))
    val echo = Module(new Ipv4IcmpEcho(packetBeats))
    val tcp = Module(new MultiTcpIpv4Endpoint(connectionCount, txBanks, txBeats, rxWords, packetBeats, ticksPerSecond, timeWaitTicks, completionDepth = completionDepth, autonomousCongestion = autonomousCongestion))
    val arp = Module(new ArpEndpoint(arpEntries, ticksPerSecond, ticksPerSecond * 60))
    val ipArbiter = Module(new PacketArbiter)
    val networkArbiter = Module(new PacketArbiter)
    val ethTx = Module(new EthernetIpv4Transmit)
    val macArbiter = Module(new PacketArbiter)
    ethernet.io.in <> io.rx; ethernet.io.localMac := io.localMac
    ethRx.io.in <> ethernet.io.ip; arp.io.in <> ethernet.io.arp
    val receiving = RegInit(false.B)
    val protocol = Reg(UInt(8.W))
    val selection = Mux(receiving, protocol, ethRx.io.out.bits.data(79, 72))
    val selectTcp = selection === 6.U; val selectUdp = selection === 17.U; val selectEcho = selection === 1.U
    tcp.io.rx.valid := ethRx.io.out.valid && selectTcp; tcp.io.rx.bits := ethRx.io.out.bits
    udpRx.io.in.valid := ethRx.io.out.valid && selectUdp; udpRx.io.in.bits := ethRx.io.out.bits
    echo.io.localIp := io.localIp
    echo.io.in.valid := ethRx.io.out.valid && selectEcho; echo.io.in.bits := ethRx.io.out.bits
    ethRx.io.out.ready := Mux(selectTcp, tcp.io.rx.ready, Mux(selectUdp, udpRx.io.in.ready, Mux(selectEcho, echo.io.in.ready, true.B)))
    when(ethRx.io.out.fire) { protocol := selection; receiving := !ethRx.io.out.bits.last }
    tcp.io.localIp := io.localIp; udpRx.io.localIp := io.localIp; udpRx.io.acceptBroadcast := true.B
    tcp.io.connections <> io.connections
    tcp.io.tick := io.tick
    tcp.io.completed <> io.completed
    udpTx.io.command <> io.udpSend; udpTx.io.in <> io.udpIn; io.udpOut <> udpRx.io.out
    io.udpSourceIp := udpRx.io.sourceIp; io.udpDestinationIp := udpRx.io.destinationIp
    io.udpSourcePort := udpRx.io.sourcePort; io.udpDestinationPort := udpRx.io.destinationPort
    io.udpLength := udpRx.io.payloadLength
    ipArbiter.io.in(0) <> udpTx.io.out; ipArbiter.io.in(1) <> tcp.io.out
    networkArbiter.io.in(0) <> ipArbiter.io.out; networkArbiter.io.in(1) <> echo.io.out
    ethTx.io.in <> networkArbiter.io.out
    ethTx.io.destinationIp := Mux(networkArbiter.io.selected, echo.io.destinationIp,
        Mux(ipArbiter.io.selected, tcp.io.packet.destinationIp, udpTx.io.destinationIp))
    ethTx.io.localIp := io.localIp; ethTx.io.localMac := io.localMac; ethTx.io.netmask := io.netmask
    ethTx.io.gateway := io.gateway; ethTx.io.mtu := io.mtu; ethTx.io.flush := io.flushArp
    arp.io.localIp := io.localIp; arp.io.localMac := io.localMac; arp.io.tick := io.tick; arp.io.flush := io.flushArp
    arp.io.resolve <> ethTx.io.resolve; ethTx.io.result <> arp.io.result
    macArbiter.io.in(0) <> arp.io.out; macArbiter.io.in(1) <> ethTx.io.out; io.tx <> macArbiter.io.out
    val routing = RegInit(false.B)
    val routingTcp = RegInit(false.B)
    val descriptor = Reg(new TcpWireDescriptor)
    val descriptorConnection = Reg(UInt(connectionBits.W))
    when(!routing && networkArbiter.io.out.valid) {
        routing := true.B; routingTcp := !networkArbiter.io.selected && ipArbiter.io.selected; descriptor := tcp.io.packet
        descriptorConnection := tcp.io.packetConnection
    }
    val routeDone = ethTx.io.dropped || (ethTx.io.out.fire && ethTx.io.out.bits.last)
    when(routeDone) { routing := false.B }
    io.txTcp := macArbiter.io.selected && routingTcp
    io.txConnection := descriptorConnection
    io.packetConnection := descriptorConnection
    io.txTag := descriptor.tag; io.txGeneration := descriptor.completion.generation
    io.routed.valid := RegNext(routeDone && routing && routingTcp, false.B)
    io.routed.bits := RegEnable({
        val result = Wire(new TcpWireResult)
        result.tag := descriptor.tag; result.generation := descriptor.completion.generation; result.failed := ethTx.io.dropped
        result
    }, routeDone && routing && routingTcp)
    io.packet := descriptor
    io.completionAccepted := tcp.io.completionAccepted
    io.outstanding := tcp.io.outstanding
    io.rxBusyDropped := tcp.io.rxBusyDropped
    io.invalidCompletion := tcp.io.invalidCompletion; io.fault := tcp.io.fault; io.conflict := arp.io.conflict
    io.rxDrops := tcp.io.rxDrops +& echo.io.dropped +& PopCount(VecInit(Seq(ethernet.io.dropped, ethRx.io.dropped,
        udpRx.io.ipDropped, udpRx.io.udpDropped, arp.io.badFrame,
        ethRx.io.out.fire && ethRx.io.out.bits.last && !selectTcp && !selectUdp && !selectEcho)))
    io.txDrops := PopCount(VecInit(Seq(udpTx.io.dropped, ethTx.io.dropped)))
}
