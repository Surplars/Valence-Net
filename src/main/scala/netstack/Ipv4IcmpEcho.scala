package netstack

import chisel3._
import chisel3.util._

class IcmpEchoMetadata extends Bundle {
    val source = UInt(32.W); val destination = UInt(32.W)
    val length = UInt(16.W); val identification = UInt(16.W)
}

/** Unicast ICMPv4 echo responder. Validate the entire IP/ICMP datagram before
  * publishing any reply, preserve identifier/sequence/payload, and recompute
  * IPv4 plus the changed ICMP checksum. Unsupported ICMP types are drained.
  * This component does not claim ICMP error/PMTU processing.
  */
class Ipv4IcmpEcho(val beatsPerPacket: Int = 128) extends Module {
    val io = IO(new Bundle {
        val localIp = Input(UInt(32.W))
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val out = Decoupled(new PacketBeat(16))
        val destinationIp = Output(UInt(32.W))
        val dropped = Output(UInt(2.W))
    })
    val ip = Module(new Ipv4Receive(beatsPerPacket))
    val checksum = Module(new InternetChecksum(16))
    val buffer = Module(new PacketCommitBuffer(16, beatsPerPacket))
    val metadata = Module(new MemoryQueue(new IcmpEchoMetadata, 2))
    val prepend = Module(new PacketPrepend(20))
    ip.io.in <> io.in; ip.io.localIp := io.localIp; ip.io.acceptBroadcast := false.B
    checksum.io.seed := 0.U
    ip.io.out.ready := checksum.io.in.ready && buffer.io.in.ready
    checksum.io.in.valid := ip.io.out.valid && buffer.io.in.ready
    checksum.io.in.bits := ip.io.out.bits
    buffer.io.in.valid := ip.io.out.valid && checksum.io.in.ready
    buffer.io.in.bits := ip.io.out.bits
    buffer.io.inContext := 0.U
    val first = RegInit(true.B)
    val good = RegInit(false.B)
    val packet = Reg(new IcmpEchoMetadata)
    val identification = RegInit(0.U(16.W))
    def fold(a: UInt): UInt = {
        val b = a(15, 0) +& (a >> 16)
        val c = b(15, 0) +& (b >> 16)
        c(15, 0)
    }
    val oldChecksum = Cat(ip.io.out.bits.data(23, 16), ip.io.out.bits.data(31, 24))
    val replyChecksum = fold(oldChecksum +& "h0800".U)
    when(first) {
        buffer.io.in.bits.data := (ip.io.out.bits.data & "hffffffffffffffffffffffff00000000".U) |
            (Cat(replyChecksum(7, 0), replyChecksum(15, 8)).pad(128) << 16)(127, 0)
    }
    when(ip.io.out.fire) {
        first := ip.io.out.bits.last
        when(first) {
            good := ip.io.protocol === 1.U && ip.io.payloadLength >= 8.U &&
                ip.io.out.bits.data(15, 0) === 8.U &&
                ip.io.source(31, 28) < 14.U && ip.io.source(31, 24) =/= 0.U &&
                ip.io.source(31, 24) =/= 127.U
            packet.source := ip.io.destination; packet.destination := ip.io.source
            packet.length := ip.io.payloadLength; packet.identification := identification
        }
    }
    val acceptable = good && !checksum.io.out.bits.error && checksum.io.out.bits.checksum === 0.U
    buffer.io.verdict.valid := checksum.io.out.valid && metadata.io.enq.ready
    buffer.io.verdict.bits := acceptable
    checksum.io.out.ready := buffer.io.verdict.ready && metadata.io.enq.ready
    metadata.io.enq.valid := checksum.io.out.valid && buffer.io.verdict.ready && acceptable && !buffer.io.dropped
    metadata.io.enq.bits := packet
    when(metadata.io.enq.fire) { identification := identification + 1.U }

    val tx = metadata.io.deq.bits
    val total = tx.length +& 20.U
    def tree(xs: Seq[UInt]): UInt = if (xs.size == 1) xs.head else tree(xs.grouped(2).map {
        case Seq(a, b) => a +& b
        case Seq(a) => a
    }.toSeq)
    val ipChecksum = ~fold(tree(Seq("h4500".U(16.W), total, tx.identification, "h4000".U(16.W),
        "h4001".U(16.W), tx.source(31, 16), tx.source(15, 0), tx.destination(31, 16), tx.destination(15, 0))))
    val header = Wire(Vec(20, UInt(8.W)))
    header.foreach(_ := 0.U)
    def word(i: Int, value: UInt): Unit = { header(i) := value(15, 8); header(i + 1) := value(7, 0) }
    header(0) := "h45".U; word(2, total); word(4, tx.identification); word(6, "h4000".U(16.W))
    header(8) := 64.U; header(9) := 1.U; word(10, ipChecksum)
    word(12, tx.source(31, 16)); word(14, tx.source(15, 0))
    word(16, tx.destination(31, 16)); word(18, tx.destination(15, 0))
    prepend.io.config.valid := metadata.io.deq.valid; metadata.io.deq.ready := prepend.io.config.ready
    prepend.io.config.bits.header := header.zipWithIndex.map { case (b, i) => (b.pad(160) << (8 * i))(159, 0) }.reduce(_ | _)
    prepend.io.config.bits.length := tx.length; prepend.io.config.bits.context := tx.destination
    prepend.io.in <> buffer.io.out; io.out <> prepend.io.out
    io.destinationIp := prepend.io.context(31, 0)
    io.dropped := PopCount(VecInit(Seq(ip.io.dropped, buffer.io.dropped)))
}
