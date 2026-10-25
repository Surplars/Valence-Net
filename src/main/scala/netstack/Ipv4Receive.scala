package netstack

import chisel3._
import chisel3.util._

/** Validate the complete IPv4 packet, then expose only its payload. */
class Ipv4Receive(val beatsPerPacket: Int = 1024, val tagged: Boolean = false) extends Module {
    val io = IO(new Bundle {
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val localIp = Input(UInt(32.W))
        val acceptBroadcast = Input(Bool())
        val out = Decoupled(new PacketBeat(16))
        val source = Output(UInt(32.W))
        val destination = Output(UInt(32.W))
        val protocol = Output(UInt(8.W))
        val payloadLength = Output(UInt(16.W))
        val dropped = Output(Bool())
        val inContext = if (tagged) Some(Input(UInt(64.W))) else None
        val outContext = if (tagged) Some(Output(UInt(64.W))) else None
        val droppedContext = if (tagged) Some(Output(UInt(64.W))) else None
    })
    val validator = Module(new Ipv4Validate)
    val buffer = Module(new PacketCommitBuffer(16, beatsPerPacket))
    val metadata = Module(new MemoryQueue(new Ipv4Verdict, 2))
    val slice = Module(new PacketSlice)
    validator.io.localIp := io.localIp
    validator.io.acceptBroadcast := io.acceptBroadcast
    buffer.io.inContext := (if (tagged) io.inContext.get else 0.U)
    io.in.ready := validator.io.in.ready && buffer.io.in.ready
    validator.io.in.valid := io.in.valid && buffer.io.in.ready
    buffer.io.in.valid := io.in.valid && validator.io.in.ready
    validator.io.in.bits := io.in.bits
    buffer.io.in.bits := io.in.bits
    buffer.io.verdict.valid := validator.io.out.valid && metadata.io.enq.ready
    buffer.io.verdict.bits := validator.io.out.bits.errors === 0.U
    validator.io.out.ready := buffer.io.verdict.ready && metadata.io.enq.ready
    metadata.io.enq.valid := validator.io.out.valid && buffer.io.verdict.ready &&
        validator.io.out.bits.errors === 0.U && !buffer.io.dropped
    metadata.io.enq.bits := validator.io.out.bits
    slice.io.config.valid := metadata.io.deq.valid
    metadata.io.deq.ready := slice.io.config.ready
    slice.io.config.bits.skip := metadata.io.deq.bits.headerLength
    slice.io.config.bits.length := metadata.io.deq.bits.totalLength - metadata.io.deq.bits.headerLength
    slice.io.config.bits.context := Cat(metadata.io.deq.bits.source, metadata.io.deq.bits.destination,
        metadata.io.deq.bits.protocol, slice.io.config.bits.length)
    slice.io.in <> buffer.io.out
    io.out <> slice.io.out
    io.source := slice.io.context(87, 56)
    io.destination := slice.io.context(55, 24)
    io.protocol := slice.io.context(23, 16)
    io.payloadLength := slice.io.context(15, 0)
    io.dropped := buffer.io.dropped
    if (tagged) {
        // The read bank is the metadata's bank at config transfer. Keep its
        // tag through slicing even after RAM output has advanced to the next bank.
        val context = RegEnable(buffer.io.outContext, slice.io.config.fire)
        io.outContext.get := context
        io.droppedContext.get := buffer.io.droppedContext
    }
}

/** IPv4-to-UDP payload receive path. Ethernet framing/ARP precede this block. */
class Ipv4UdpReceive(val beatsPerPacket: Int = 1024) extends Module {
    val io = IO(new Bundle {
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val localIp = Input(UInt(32.W))
        val acceptBroadcast = Input(Bool())
        val out = Decoupled(new PacketBeat(16))
        val sourceIp = Output(UInt(32.W))
        val destinationIp = Output(UInt(32.W))
        val sourcePort = Output(UInt(16.W))
        val destinationPort = Output(UInt(16.W))
        val payloadLength = Output(UInt(16.W))
        val ipDropped = Output(Bool())
        val udpDropped = Output(Bool())
        val otherProtocol = Output(Bool())
    })
    val ip = Module(new Ipv4Receive(beatsPerPacket))
    val udp = Module(new UdpReceive(beatsPerPacket))
    val payload = Module(new PacketSlice)
    ip.io.in <> io.in
    ip.io.localIp := io.localIp
    ip.io.acceptBroadcast := io.acceptBroadcast
    val selected = ip.io.protocol === 17.U
    ip.io.out.ready := !selected || udp.io.in.ready
    udp.io.in.valid := ip.io.out.valid && selected
    udp.io.in.bits := ip.io.out.bits
    udp.io.sourceIp := ip.io.source
    udp.io.destinationIp := ip.io.destination
    udp.io.ipPayloadLength := ip.io.payloadLength
    val needConfig = RegInit(true.B)
    def word(i: Int): UInt = Cat(udp.io.out.bits.data(8 * i + 7, 8 * i),
        udp.io.out.bits.data(8 * i + 15, 8 * i + 8))
    payload.io.config.valid := needConfig && udp.io.out.valid
    payload.io.config.bits.skip := 8.U
    payload.io.config.bits.length := word(4) - 8.U
    payload.io.config.bits.context := Cat(udp.io.outSourceIp, udp.io.outDestinationIp,
        word(0), word(2), payload.io.config.bits.length)
    udp.io.out.ready := !needConfig && payload.io.in.ready
    payload.io.in.valid := udp.io.out.valid && !needConfig
    payload.io.in.bits := udp.io.out.bits
    when(payload.io.config.fire) { needConfig := false.B }
    when(udp.io.out.fire && udp.io.out.bits.last) { needConfig := true.B }
    io.out <> payload.io.out
    io.sourceIp := payload.io.context(111, 80)
    io.destinationIp := payload.io.context(79, 48)
    io.sourcePort := payload.io.context(47, 32)
    io.destinationPort := payload.io.context(31, 16)
    io.payloadLength := payload.io.context(15, 0)
    io.ipDropped := ip.io.dropped
    io.udpDropped := udp.io.dropped
    io.otherProtocol := ip.io.out.fire && ip.io.out.bits.last && !selected
}

class IpReceiveHarness extends Module {
    val io = IO(new Bundle {
        val lo = Input(UInt(64.W))
        val hi = Input(UInt(64.W))
        val keep = Input(UInt(16.W))
        val last = Input(Bool())
        val error = Input(Bool())
        val valid = Input(Bool())
        val ready = Output(Bool())
        val localIp = Input(UInt(32.W))
        val broadcast = Input(Bool())
        val outReady = Input(Bool())
        val outValid = Output(Bool())
        val outLo = Output(UInt(64.W))
        val outHi = Output(UInt(64.W))
        val outKeep = Output(UInt(16.W))
        val outLast = Output(Bool())
        val outError = Output(Bool())
        val sourceIp = Output(UInt(32.W))
        val destinationIp = Output(UInt(32.W))
        val sourcePort = Output(UInt(16.W))
        val destinationPort = Output(UInt(16.W))
        val length = Output(UInt(16.W))
        val ipDrops = Output(Bool())
        val udpDrops = Output(Bool())
        val other = Output(Bool())
    })
    val dut = Module(new Ipv4UdpReceive(32))
    dut.io.in.valid := io.valid
    dut.io.in.bits.data := Cat(io.hi, io.lo)
    dut.io.in.bits.keep := io.keep
    dut.io.in.bits.last := io.last
    dut.io.in.bits.error := io.error
    io.ready := dut.io.in.ready
    dut.io.localIp := io.localIp
    dut.io.acceptBroadcast := io.broadcast
    dut.io.out.ready := io.outReady
    io.outValid := dut.io.out.valid
    io.outLo := dut.io.out.bits.data(63, 0)
    io.outHi := dut.io.out.bits.data(127, 64)
    io.outKeep := dut.io.out.bits.keep
    io.outLast := dut.io.out.bits.last
    io.outError := dut.io.out.bits.error
    io.sourceIp := dut.io.sourceIp
    io.destinationIp := dut.io.destinationIp
    io.sourcePort := dut.io.sourcePort
    io.destinationPort := dut.io.destinationPort
    io.length := dut.io.payloadLength
    io.ipDrops := dut.io.ipDropped
    io.udpDrops := dut.io.udpDropped
    io.other := dut.io.otherProtocol
}
