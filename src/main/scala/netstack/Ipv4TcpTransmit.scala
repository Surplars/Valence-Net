package netstack

import chisel3._
import chisel3.util._

class TcpSendCommand extends Bundle {
    val sourceIp = UInt(32.W)
    val destinationIp = UInt(32.W)
    val sourcePort = UInt(16.W)
    val destinationPort = UInt(16.W)
    val sequence = UInt(32.W)
    val acknowledgement = UInt(32.W)
    val flags = UInt(8.W)
    val window = UInt(16.W)
    val urgent = UInt(16.W)
    val length = UInt(16.W)
    val identification = UInt(16.W)
    val ttl = UInt(8.W)
    val tos = UInt(8.W)
    // Option bytes are already encoded/padded by the connection engine.
    val optionLength = UInt(6.W)
    val options = UInt(320.W)
}

class TcpSendMetadata extends Bundle {
    val command = new TcpSendCommand
    val checksum = UInt(16.W)
}

/** Validates/retains one TCP payload, computes the mandatory TCP checksum and
  * emits an unfragmented IPv4 datagram. This is a serializer, not connection
  * state or a retransmission store: payload ownership beyond emission belongs
  * to the TCP engine. Options must be a caller-encoded multiple of four bytes.
  */
class Ipv4TcpTransmit(val beatsPerPacket: Int = 1024) extends Module {
    val io = IO(new Bundle {
        val command = Flipped(Decoupled(new TcpSendCommand))
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val out = Decoupled(new PacketBeat(16))
        val destinationIp = Output(UInt(32.W))
        val dropped = Output(Bool())
    })
    def tree(xs: Seq[UInt]): UInt = if (xs.size == 1) xs.head else tree(xs.grouped(2).map {
        case Seq(a, b) => a +& b
        case Seq(a) => a
    }.toSeq)
    def fold(sum: UInt): UInt = {
        val a = sum(15, 0) +& (sum >> 16)
        val b = a(15, 0) +& (a >> 16)
        b(15, 0)
    }
    val command = Reg(new TcpSendCommand)
    val active = RegInit(false.B)
    val waiting = RegInit(false.B)
    val seedPending = RegInit(false.B)
    val seed = Reg(UInt(16.W))
    val tcpLength = Reg(UInt(18.W))
    val offsetFlags = Reg(UInt(16.W))
    val optionWords = Reg(Vec(20, UInt(16.W)))
    val count = RegInit(0.U(17.W))
    val bad = RegInit(false.B)
    val buffer = Module(new PacketCommitBuffer(16, beatsPerPacket))
    val checksum = Module(new InternetChecksum(16))
    val metadata = Module(new Queue(new TcpSendMetadata, 2))
    val prepend = Module(new VariablePacketPrepend(80))
    io.command.ready := !active && !waiting && !seedPending
    val requestedTotal = io.command.bits.length +& io.command.bits.optionLength +& 40.U
    when(io.command.fire) {
        command := io.command.bits
        // Retime fixed header derivatives with command capture so option
        // masking/length addition do not precede the seed reduction stage.
        tcpLength := io.command.bits.length +& io.command.bits.optionLength +& 20.U
        offsetFlags := Cat((5.U(4.W) + (io.command.bits.optionLength >> 2))(3, 0),
            0.U(4.W), io.command.bits.flags)
        for (i <- 0 until 20) {
            optionWords(i) := Mux((2 * i).U < io.command.bits.optionLength,
                Cat(io.command.bits.options(16 * i + 7, 16 * i),
                    io.command.bits.options(16 * i + 15, 16 * i + 8)), 0.U(16.W))
        }
        active := true.B
        seedPending := true.B
        count := 0.U
        bad := requestedTotal > 65535.U || io.command.bits.ttl === 0.U ||
            io.command.bits.optionLength > 40.U || io.command.bits.optionLength(1, 0) =/= 0.U
    }
    val seedWords = Seq(command.sourceIp(31, 16), command.sourceIp(15, 0),
        command.destinationIp(31, 16), command.destinationIp(15, 0), 6.U(16.W), tcpLength,
        command.sourcePort, command.destinationPort, command.sequence(31, 16), command.sequence(15, 0),
        command.acknowledgement(31, 16), command.acknowledgement(15, 0), offsetFlags,
        command.window, command.urgent) ++ optionWords.toSeq
    // The latched command's pseudo/header seed commits before its first
    // payload beat. This splits two complete checksum reductions, rather than
    // chaining option-length/header arithmetic into the payload checksum.
    when(seedPending) {
        seed := OnesComplement.sum16(seedWords)
        seedPending := false.B
    }
    checksum.io.seed := seed
    val payloadActive = active && !seedPending
    io.in.ready := payloadActive && buffer.io.in.ready && checksum.io.in.ready
    buffer.io.in.valid := payloadActive && io.in.valid && checksum.io.in.ready
    checksum.io.in.valid := payloadActive && io.in.valid && buffer.io.in.ready
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
    // Unlike IPv4 UDP, a TCP checksum value of zero is transmitted unchanged.
    metadata.io.enq.bits.checksum := checksum.io.out.bits.checksum
    when(buffer.io.verdict.fire) { waiting := false.B }
    io.dropped := buffer.io.dropped

    val tx = metadata.io.deq.bits.command
    val prefixLength = tx.optionLength +& 40.U
    val totalLength = tx.length + prefixLength
    val headerSum = OnesComplement.sum16(Seq("h4500".U(16.W) | tx.tos, totalLength, tx.identification,
        "h4000".U(16.W), Cat(tx.ttl, 6.U(8.W)), tx.sourceIp(31, 16), tx.sourceIp(15, 0),
        tx.destinationIp(31, 16), tx.destinationIp(15, 0)))
    val header = Wire(Vec(80, UInt(8.W)))
    header.foreach(_ := 0.U)
    def setWord(i: Int, value: UInt): Unit = { header(i) := value(15, 8); header(i + 1) := value(7, 0) }
    def setAddress(i: Int, value: UInt): Unit = { setWord(i, value(31, 16)); setWord(i + 2, value(15, 0)) }
    header(0) := "h45".U
    header(1) := tx.tos
    setWord(2, totalLength)
    setWord(4, tx.identification)
    setWord(6, "h4000".U(16.W))
    header(8) := tx.ttl
    header(9) := 6.U
    setWord(10, ~headerSum)
    setAddress(12, tx.sourceIp)
    setAddress(16, tx.destinationIp)
    setWord(20, tx.sourcePort)
    setWord(22, tx.destinationPort)
    setAddress(24, tx.sequence)
    setAddress(28, tx.acknowledgement)
    header(32) := Cat((5.U(4.W) + (tx.optionLength >> 2))(3, 0), 0.U(4.W))
    header(33) := tx.flags
    setWord(34, tx.window)
    setWord(36, metadata.io.deq.bits.checksum)
    setWord(38, tx.urgent)
    for (i <- 0 until 40) header(40 + i) := tx.options(8 * i + 7, 8 * i)
    prepend.io.config.valid := metadata.io.deq.valid
    metadata.io.deq.ready := prepend.io.config.ready
    // Explicit wide placement also avoids the pinned GSIM constant-Cat issue.
    prepend.io.config.bits.header := header.zipWithIndex.map { case (byte, i) =>
        (byte.pad(640) << (8 * i))(639, 0)
    }.reduce(_ | _)
    prepend.io.config.bits.headerLength := prefixLength
    prepend.io.config.bits.length := tx.length
    prepend.io.config.bits.context := tx.destinationIp
    prepend.io.in <> buffer.io.out
    io.out <> prepend.io.out
    io.destinationIp := prepend.io.context(31, 0)
}

class TcpTransmitHarness extends Module {
    val io = IO(new Bundle {
        val commandValid = Input(Bool())
        val commandReady = Output(Bool())
        val sourceIp = Input(UInt(32.W))
        val destinationIp = Input(UInt(32.W))
        val sourcePort = Input(UInt(16.W))
        val destinationPort = Input(UInt(16.W))
        val sequence = Input(UInt(32.W))
        val acknowledgement = Input(UInt(32.W))
        val flags = Input(UInt(8.W))
        val window = Input(UInt(16.W))
        val urgent = Input(UInt(16.W))
        val length = Input(UInt(16.W))
        val identification = Input(UInt(16.W))
        val ttl = Input(UInt(8.W))
        val tos = Input(UInt(8.W))
        val optionLength = Input(UInt(6.W))
        val options0 = Input(UInt(64.W))
        val options1 = Input(UInt(64.W))
        val options2 = Input(UInt(64.W))
        val options3 = Input(UInt(64.W))
        val options4 = Input(UInt(64.W))
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
    val dut = Module(new Ipv4TcpTransmit(32))
    dut.io.command.valid := io.commandValid
    io.commandReady := dut.io.command.ready
    dut.io.command.bits.sourceIp := io.sourceIp
    dut.io.command.bits.destinationIp := io.destinationIp
    dut.io.command.bits.sourcePort := io.sourcePort
    dut.io.command.bits.destinationPort := io.destinationPort
    dut.io.command.bits.sequence := io.sequence
    dut.io.command.bits.acknowledgement := io.acknowledgement
    dut.io.command.bits.flags := io.flags
    dut.io.command.bits.window := io.window
    dut.io.command.bits.urgent := io.urgent
    dut.io.command.bits.length := io.length
    dut.io.command.bits.identification := io.identification
    dut.io.command.bits.ttl := io.ttl
    dut.io.command.bits.tos := io.tos
    dut.io.command.bits.optionLength := io.optionLength
    dut.io.command.bits.options := Cat(io.options4, io.options3, io.options2, io.options1, io.options0)
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
