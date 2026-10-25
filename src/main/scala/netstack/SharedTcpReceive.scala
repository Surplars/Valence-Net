package netstack

import chisel3._
import chisel3.util._

/** One full-packet IPv4/TCP validator shared by all connection contexts.
  * The caller's immutable tag follows packet-bank ownership through commits,
  * header slicing and every rejection path. It is not inferred from live tuples.
  */
class SharedTcpReceive(val packetBeats: Int = 128) extends Module {
    val io = IO(new Bundle {
        val localIp = Input(UInt(32.W))
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val inContext = Input(UInt(64.W))
        val metadata = Decoupled(new TcpSegment)
        val metadataContext = Output(UInt(64.W))
        val payload = Decoupled(new PacketBeat(16))
        val drops = Vec(3, Valid(UInt(64.W)))
    })
    val ip = Module(new Ipv4Receive(packetBeats, tagged = true))
    val tcp = Module(new TcpReceive(packetBeats, tagged = true))
    ip.io.in <> io.in; ip.io.inContext.get := io.inContext
    ip.io.localIp := io.localIp; ip.io.acceptBroadcast := false.B
    val selected = ip.io.protocol === 6.U
    tcp.io.in.valid := ip.io.out.valid && selected
    tcp.io.in.bits := ip.io.out.bits
    ip.io.out.ready := !selected || tcp.io.in.ready
    tcp.io.inContext.get := ip.io.outContext.get
    tcp.io.sourceIp := ip.io.source; tcp.io.destinationIp := ip.io.destination
    tcp.io.ipPayloadLength := ip.io.payloadLength
    io.metadata <> tcp.io.metadata; io.payload <> tcp.io.payload
    io.metadataContext := tcp.io.metadataContext.get
    io.drops(0).valid := ip.io.dropped; io.drops(0).bits := ip.io.droppedContext.get
    io.drops(1).valid := tcp.io.dropped; io.drops(1).bits := tcp.io.droppedContext.get
    io.drops(2).valid := ip.io.out.fire && ip.io.out.bits.last && !selected
    io.drops(2).bits := ip.io.outContext.get
}
