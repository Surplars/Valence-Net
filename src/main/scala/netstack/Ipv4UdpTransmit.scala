package netstack

import chisel3._
import chisel3.util._

class UdpSendCommand extends Bundle {
    val sourceIp = UInt(32.W)
    val destinationIp = UInt(32.W)
    val sourcePort = UInt(16.W)
    val destinationPort = UInt(16.W)
    val length = UInt(16.W)
    val identification = UInt(16.W)
    val ttl = UInt(8.W)
    val tos = UInt(8.W)
}

class UdpSendMetadata extends Bundle {
    val command = new UdpSendCommand
    val checksum = UInt(16.W)
}

/** Autonomous unfragmented IPv4/UDP transmitter. One command precedes each
  * payload; even a zero-length payload supplies a last beat. Data is retained
  * until length, stream integrity and checksums are known. Bad/oversized input
  * is drained and dropped. Generates both checksums and sets IPv4 DF.
  */
class Ipv4UdpTransmit(val beatsPerPacket: Int = 1024) extends Module {
    val io = IO(new Bundle {
        val command = Flipped(Decoupled(new UdpSendCommand))
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val out = Decoupled(new PacketBeat(16))
        val destinationIp = Output(UInt(32.W))
        val dropped = Output(Bool())
    })
    val command = Reg(new UdpSendCommand)
    val active = RegInit(false.B)
    val waiting = RegInit(false.B)
    val count = RegInit(0.U(17.W))
    val bad = RegInit(false.B)
    val buffer = Module(new PacketCommitBuffer(16, beatsPerPacket))
    val checksum = Module(new InternetChecksum(16))
    val metadata = Module(new MemoryQueue(new UdpSendMetadata, 2))
    val prepend = Module(new PacketPrepend(28))
    io.command.ready := !active && !waiting
    when(io.command.fire) {
        command := io.command.bits
        active := true.B
        count := 0.U
        bad := io.command.bits.length > 65507.U || io.command.bits.ttl === 0.U
    }
    val udpLength = command.length +& 8.U
    checksum.io.seed := OnesComplement.sum16(Seq(command.sourceIp(31, 16), command.sourceIp(15, 0),
        command.destinationIp(31, 16), command.destinationIp(15, 0),
        command.sourcePort, command.destinationPort, udpLength, udpLength, 17.U(16.W)))
    io.in.ready := active && buffer.io.in.ready && checksum.io.in.ready
    buffer.io.in.valid := active && io.in.valid && checksum.io.in.ready
    checksum.io.in.valid := active && io.in.valid && buffer.io.in.ready
    buffer.io.in.bits := io.in.bits
    checksum.io.in.bits := io.in.bits
    buffer.io.inContext := 0.U
    val nextCount = count +& PopCount(io.in.bits.keep)
    val legalKeep = (io.in.bits.keep & (io.in.bits.keep + 1.U)) === 0.U
    when(io.in.fire) {
        count := Mux(nextCount > 131071.U, 131071.U, nextCount)
        bad := bad || !legalKeep || io.in.bits.error ||
            (!io.in.bits.last && io.in.bits.keep =/= "hffff".U) ||
            (io.in.bits.last && nextCount =/= command.length)
        when(io.in.bits.last) { active := false.B; waiting := true.B }
    }
    val acceptable = !bad && !checksum.io.out.bits.error
    buffer.io.verdict.valid := waiting && checksum.io.out.valid && metadata.io.enq.ready
    buffer.io.verdict.bits := acceptable
    checksum.io.out.ready := waiting && buffer.io.verdict.ready && metadata.io.enq.ready
    metadata.io.enq.valid := waiting && checksum.io.out.valid && buffer.io.verdict.ready &&
        acceptable && !buffer.io.dropped
    metadata.io.enq.bits.command := command
    metadata.io.enq.bits.checksum := Mux(checksum.io.out.bits.checksum === 0.U, "hffff".U, checksum.io.out.bits.checksum)
    when(buffer.io.verdict.fire) { waiting := false.B }
    io.dropped := buffer.io.dropped

    val tx = metadata.io.deq.bits.command
    val totalLength = tx.length + 28.U
    val headerChecksum = ~OnesComplement.sum16(Seq("h4500".U(16.W), tx.tos, totalLength,
        tx.identification, "h4000".U(16.W), Cat(tx.ttl, 17.U(8.W)),
        tx.sourceIp(31, 16), tx.sourceIp(15, 0), tx.destinationIp(31, 16), tx.destinationIp(15, 0)))
    val header = Wire(Vec(28, UInt(8.W)))
    header.foreach(_ := 0.U)
    def setWord(i: Int, value: UInt): Unit = { header(i) := value(15, 8); header(i + 1) := value(7, 0) }
    def setAddress(i: Int, value: UInt): Unit = { setWord(i, value(31, 16)); setWord(i + 2, value(15, 0)) }
    header(0) := "h45".U
    header(1) := tx.tos
    setWord(2, totalLength)
    setWord(4, tx.identification)
    setWord(6, "h4000".U(16.W))
    header(8) := tx.ttl
    header(9) := 17.U
    setWord(10, headerChecksum)
    setAddress(12, tx.sourceIp)
    setAddress(16, tx.destinationIp)
    setWord(20, tx.sourcePort)
    setWord(22, tx.destinationPort)
    setWord(24, tx.length + 8.U)
    setWord(26, metadata.io.deq.bits.checksum)
    prepend.io.config.valid := metadata.io.deq.valid
    metadata.io.deq.ready := prepend.io.config.ready
    prepend.io.config.bits.header := header.asUInt
    prepend.io.config.bits.length := tx.length
    prepend.io.config.bits.context := tx.destinationIp
    prepend.io.in <> buffer.io.out
    io.out <> prepend.io.out
    io.destinationIp := prepend.io.context(31, 0)
}

class TransmitHarness extends Module {
    val io = IO(new Bundle {
        val commandValid = Input(Bool())
        val commandReady = Output(Bool())
        val sourceIp = Input(UInt(32.W))
        val destinationIp = Input(UInt(32.W))
        val sourcePort = Input(UInt(16.W))
        val destinationPort = Input(UInt(16.W))
        val length = Input(UInt(16.W))
        val identification = Input(UInt(16.W))
        val ttl = Input(UInt(8.W))
        val tos = Input(UInt(8.W))
        val lo = Input(UInt(64.W))
        val hi = Input(UInt(64.W))
        val keep = Input(UInt(16.W))
        val last = Input(Bool())
        val error = Input(Bool())
        val valid = Input(Bool())
        val ready = Output(Bool())
        val outReady = Input(Bool())
        val outValid = Output(Bool())
        val outLo = Output(UInt(64.W))
        val outHi = Output(UInt(64.W))
        val outKeep = Output(UInt(16.W))
        val outLast = Output(Bool())
        val outError = Output(Bool())
        val outDestination = Output(UInt(32.W))
        val dropped = Output(Bool())
    })
    val dut = Module(new Ipv4UdpTransmit(32))
    dut.io.command.valid := io.commandValid
    io.commandReady := dut.io.command.ready
    dut.io.command.bits.sourceIp := io.sourceIp
    dut.io.command.bits.destinationIp := io.destinationIp
    dut.io.command.bits.sourcePort := io.sourcePort
    dut.io.command.bits.destinationPort := io.destinationPort
    dut.io.command.bits.length := io.length
    dut.io.command.bits.identification := io.identification
    dut.io.command.bits.ttl := io.ttl
    dut.io.command.bits.tos := io.tos
    dut.io.in.valid := io.valid
    dut.io.in.bits.data := Cat(io.hi, io.lo)
    dut.io.in.bits.keep := io.keep
    dut.io.in.bits.last := io.last
    dut.io.in.bits.error := io.error
    io.ready := dut.io.in.ready
    dut.io.out.ready := io.outReady
    io.outValid := dut.io.out.valid
    io.outLo := dut.io.out.bits.data(63, 0)
    io.outHi := dut.io.out.bits.data(127, 64)
    io.outKeep := dut.io.out.bits.keep
    io.outLast := dut.io.out.bits.last
    io.outError := dut.io.out.bits.error
    io.outDestination := dut.io.destinationIp
    io.dropped := dut.io.dropped
}
