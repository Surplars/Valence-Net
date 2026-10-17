package netstack

import chisel3._
import chisel3.util._

/** MAC-facing Ethernet-II IPv4/UDP endpoint. FCS/preamble/IFG and line-rate
  * ingress buffering belong to the MAC adapter. The payload ports are internal
  * hardware streams; they are not a committed Linux/DMA API.
  */
class EthernetUdpEndpoint(val beatsPerPacket: Int = 1024, val arpEntries: Int = 16,
    val retryTicks: Int = 1000, val lifetimeTicks: Int = 60000) extends Module {
    val io = IO(new Bundle {
        val localIp = Input(UInt(32.W)); val localMac = Input(UInt(48.W))
        val netmask = Input(UInt(32.W)); val gateway = Input(UInt(32.W)); val mtu = Input(UInt(16.W))
        val tick = Input(Bool()); val flushArp = Input(Bool()); val acceptBroadcast = Input(Bool())
        val rx = Flipped(Decoupled(new PacketBeat(16)))
        val tx = Decoupled(new PacketBeat(16))
        val send = Flipped(Decoupled(new UdpSendCommand))
        val payloadIn = Flipped(Decoupled(new PacketBeat(16)))
        val payloadOut = Decoupled(new PacketBeat(16))
        val sourceIp = Output(UInt(32.W)); val destinationIp = Output(UInt(32.W))
        val sourcePort = Output(UInt(16.W)); val destinationPort = Output(UInt(16.W))
        val payloadLength = Output(UInt(16.W))
        val rxDrops = Output(UInt(3.W)); val txDrops = Output(UInt(2.W)); val conflict = Output(Bool())
    })
    val dispatch = Module(new EthernetDispatch)
    val ethRx = Module(new EthernetIpv4Receive(beatsPerPacket))
    val ipRx = Module(new Ipv4UdpReceive(beatsPerPacket))
    val ipTx = Module(new Ipv4UdpTransmit(beatsPerPacket))
    val ethTx = Module(new EthernetIpv4Transmit)
    val arp = Module(new ArpEndpoint(arpEntries, retryTicks, lifetimeTicks))
    val arbiter = Module(new PacketArbiter)
    dispatch.io.in <> io.rx
    dispatch.io.localMac := io.localMac
    ethRx.io.in <> dispatch.io.ip
    arp.io.in <> dispatch.io.arp
    ipRx.io.in <> ethRx.io.out
    ipRx.io.localIp := io.localIp
    ipRx.io.acceptBroadcast := io.acceptBroadcast
    io.payloadOut <> ipRx.io.out
    io.sourceIp := ipRx.io.sourceIp; io.destinationIp := ipRx.io.destinationIp
    io.sourcePort := ipRx.io.sourcePort; io.destinationPort := ipRx.io.destinationPort
    io.payloadLength := ipRx.io.payloadLength
    ipTx.io.command <> io.send
    ipTx.io.in <> io.payloadIn
    ethTx.io.in <> ipTx.io.out
    ethTx.io.destinationIp := ipTx.io.destinationIp
    ethTx.io.localIp := io.localIp; ethTx.io.localMac := io.localMac
    ethTx.io.netmask := io.netmask; ethTx.io.gateway := io.gateway; ethTx.io.mtu := io.mtu
    ethTx.io.flush := io.flushArp
    arp.io.localIp := io.localIp; arp.io.localMac := io.localMac
    arp.io.tick := io.tick; arp.io.flush := io.flushArp
    arp.io.resolve <> ethTx.io.resolve
    ethTx.io.result <> arp.io.result
    arbiter.io.in(0) <> arp.io.out
    arbiter.io.in(1) <> ethTx.io.out
    io.tx <> arbiter.io.out
    io.rxDrops := PopCount(VecInit(Seq(dispatch.io.dropped, ethRx.io.dropped,
        ipRx.io.ipDropped, ipRx.io.udpDropped, ipRx.io.otherProtocol, arp.io.badFrame)))
    io.txDrops := PopCount(VecInit(Seq(ipTx.io.dropped, ethTx.io.dropped)))
    io.conflict := arp.io.conflict
}

class EthernetHarness extends Module {
    val io = IO(new Bundle {
        val rxLo = Input(UInt(64.W)); val rxHi = Input(UInt(64.W)); val rxKeep = Input(UInt(16.W))
        val rxLast = Input(Bool()); val rxError = Input(Bool()); val rxValid = Input(Bool()); val rxReady = Output(Bool())
        val txLo = Output(UInt(64.W)); val txHi = Output(UInt(64.W)); val txKeep = Output(UInt(16.W))
        val txLast = Output(Bool()); val txError = Output(Bool()); val txValid = Output(Bool()); val txReady = Input(Bool())
        val commandValid = Input(Bool()); val commandReady = Output(Bool())
        val destinationIp = Input(UInt(32.W)); val sourcePort = Input(UInt(16.W)); val destinationPort = Input(UInt(16.W))
        val length = Input(UInt(16.W)); val identification = Input(UInt(16.W))
        val inLo = Input(UInt(64.W)); val inHi = Input(UInt(64.W)); val inKeep = Input(UInt(16.W))
        val inLast = Input(Bool()); val inError = Input(Bool()); val inValid = Input(Bool()); val inReady = Output(Bool())
        val outLo = Output(UInt(64.W)); val outHi = Output(UInt(64.W)); val outKeep = Output(UInt(16.W))
        val outLast = Output(Bool()); val outError = Output(Bool()); val outValid = Output(Bool()); val outReady = Input(Bool())
        val outSourceIp = Output(UInt(32.W)); val outDestinationIp = Output(UInt(32.W))
        val outSourcePort = Output(UInt(16.W)); val outDestinationPort = Output(UInt(16.W)); val outLength = Output(UInt(16.W))
        val tick = Input(Bool()); val flush = Input(Bool()); val gateway = Input(UInt(32.W)); val mtu = Input(UInt(16.W))
        val rxDrops = Output(UInt(3.W)); val txDrops = Output(UInt(2.W))
    })
    val dut = Module(new EthernetUdpEndpoint(64, 4, 8, 200))
    dut.io.localIp := "hc0000202".U; dut.io.localMac := "h020000000002".U
    dut.io.netmask := "hffffff00".U; dut.io.gateway := io.gateway; dut.io.mtu := io.mtu
    dut.io.tick := io.tick; dut.io.flushArp := io.flush; dut.io.acceptBroadcast := true.B
    dut.io.rx.valid := io.rxValid; io.rxReady := dut.io.rx.ready
    dut.io.rx.bits.data := Cat(io.rxHi, io.rxLo); dut.io.rx.bits.keep := io.rxKeep
    dut.io.rx.bits.last := io.rxLast; dut.io.rx.bits.error := io.rxError
    dut.io.tx.ready := io.txReady; io.txValid := dut.io.tx.valid
    io.txLo := dut.io.tx.bits.data(63, 0); io.txHi := dut.io.tx.bits.data(127, 64)
    io.txKeep := dut.io.tx.bits.keep; io.txLast := dut.io.tx.bits.last; io.txError := dut.io.tx.bits.error
    dut.io.send.valid := io.commandValid; io.commandReady := dut.io.send.ready
    dut.io.send.bits.sourceIp := "hc0000202".U; dut.io.send.bits.destinationIp := io.destinationIp
    dut.io.send.bits.sourcePort := io.sourcePort; dut.io.send.bits.destinationPort := io.destinationPort
    dut.io.send.bits.length := io.length; dut.io.send.bits.identification := io.identification
    dut.io.send.bits.ttl := 64.U; dut.io.send.bits.tos := 0.U
    dut.io.payloadIn.valid := io.inValid; io.inReady := dut.io.payloadIn.ready
    dut.io.payloadIn.bits.data := Cat(io.inHi, io.inLo); dut.io.payloadIn.bits.keep := io.inKeep
    dut.io.payloadIn.bits.last := io.inLast; dut.io.payloadIn.bits.error := io.inError
    dut.io.payloadOut.ready := io.outReady; io.outValid := dut.io.payloadOut.valid
    io.outLo := dut.io.payloadOut.bits.data(63, 0); io.outHi := dut.io.payloadOut.bits.data(127, 64)
    io.outKeep := dut.io.payloadOut.bits.keep; io.outLast := dut.io.payloadOut.bits.last; io.outError := dut.io.payloadOut.bits.error
    io.outSourceIp := dut.io.sourceIp; io.outDestinationIp := dut.io.destinationIp
    io.outSourcePort := dut.io.sourcePort; io.outDestinationPort := dut.io.destinationPort; io.outLength := dut.io.payloadLength
    io.rxDrops := dut.io.rxDrops; io.txDrops := dut.io.txDrops
}
