package netstack

import chisel3._
import chisel3.util._

class TcpOptions extends Bundle {
    val mssPresent = Bool(); val mss = UInt(16.W)
    val scalePresent = Bool(); val scale = UInt(4.W)
    val sackPermitted = Bool()
    val timestampPresent = Bool(); val timestamp = UInt(32.W); val timestampEcho = UInt(32.W)
    val sackCount = UInt(3.W)
    val sackLeft = Vec(4, UInt(32.W)); val sackRight = Vec(4, UInt(32.W))
}

class TcpSegment extends Bundle {
    val sourceIp = UInt(32.W); val destinationIp = UInt(32.W)
    val sourcePort = UInt(16.W); val destinationPort = UInt(16.W)
    val sequence = UInt(32.W); val acknowledgement = UInt(32.W)
    val flags = UInt(8.W); val window = UInt(16.W); val urgent = UInt(16.W)
    val headerLength = UInt(6.W); val payloadLength = UInt(16.W)
    val options = new TcpOptions
    // bit 0 stream, bit 1 header/length, bit 2 checksum, bit 3 option encoding
    val errors = UInt(4.W)
}

/** Validate a complete IPv4 TCP segment. Input starts at the TCP header and
  * metadata is sampled at the first beat. Reserved header bits are ignored.
  * Options parse one option per cycle, overlapping payload arrival. Unknown
  * well-formed options are skipped. First recognized duplicate wins; SYN-only
  * options are ignored outside SYN segments. WS is clamped to 14 (RFC 7323).
  * Parsing SACK metadata does not by itself implement SACK recovery.
  */
class TcpValidate extends Module {
    val io = IO(new Bundle {
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val sourceIp = Input(UInt(32.W)); val destinationIp = Input(UInt(32.W))
        val ipPayloadLength = Input(UInt(16.W))
        val out = Decoupled(new TcpSegment)
    })
    val active = RegInit(false.B)
    val waiting = RegInit(false.B)
    val count = RegInit(0.U(17.W))
    val header = Reg(Vec(4, UInt(128.W)))
    val wordIndex = RegInit(0.U(3.W))
    val headerLength = Reg(UInt(6.W))
    val length = Reg(UInt(16.W))
    val source = Reg(UInt(32.W)); val destination = Reg(UInt(32.W))
    val streamBad = RegInit(false.B); val lengthBad = RegInit(false.B)
    val headerReady = RegInit(false.B)
    val parseDone = RegInit(false.B)
    val optionBad = RegInit(false.B)
    val parseIndex = RegInit(20.U(6.W))
    val options = RegInit(0.U.asTypeOf(new TcpOptions))
    def byte(i: Int): UInt = header(i / 16)(8 * (i % 16) + 7, 8 * (i % 16))
    def field(i: Int, n: Int): UInt = Cat((i until i + n).map(byte))
    val checksum = Module(new InternetChecksum(16))
    checksum.io.seed := OnesComplement.sum16(Seq(io.sourceIp(31, 16), io.sourceIp(15, 0),
        io.destinationIp(31, 16), io.destinationIp(15, 0), io.ipPayloadLength, 6.U(16.W)))
    checksum.io.in.valid := io.in.valid && !waiting
    checksum.io.in.bits := io.in.bits
    io.in.ready := !waiting && checksum.io.in.ready
    val firstHeader = Cat(io.in.bits.data(103, 100), 0.U(2.W))
    val hdrLength = Mux(active, headerLength, firstHeader)
    val expectedLength = Mux(active, length, io.ipPayloadLength)
    val nextCount = count +& PopCount(io.in.bits.keep)
    val legalKeep = (io.in.bits.keep & (io.in.bits.keep + 1.U)) === 0.U
    when(io.in.fire) {
        when(!active) {
            source := io.sourceIp; destination := io.destinationIp
            headerLength := firstHeader; length := io.ipPayloadLength
        }
        when(wordIndex < 4.U) { header(wordIndex(1, 0)) := io.in.bits.data; wordIndex := wordIndex + 1.U }
        count := Mux(nextCount > 131071.U, 131071.U, nextCount)
        streamBad := streamBad || io.in.bits.error || !legalKeep || io.in.bits.keep === 0.U ||
            (!io.in.bits.last && io.in.bits.keep =/= "hffff".U)
        lengthBad := lengthBad || hdrLength < 20.U || expectedLength < hdrLength ||
            (io.in.bits.last && (nextCount =/= expectedLength || nextCount < 20.U))
        when(nextCount >= hdrLength || io.in.bits.last) { headerReady := true.B }
        active := !io.in.bits.last
        when(io.in.bits.last) { waiting := true.B }
    }
    val optionWindow = (header.asUInt >> (parseIndex << 3))(271, 0)
    def optionField(i: Int, n: Int): UInt = Cat((i until i + n).map(j => optionWindow(8 * j + 7, 8 * j)))
    val kind = optionField(0, 1)
    val optionLength = optionField(1, 1)
    val end = parseIndex +& optionLength
    val syn = byte(13)(1)
    when(headerReady && !parseDone) {
        when(parseIndex >= headerLength) { parseDone := true.B }
            .elsewhen(parseIndex >= count) { parseDone := true.B; optionBad := true.B }
            .elsewhen(kind === 0.U) { parseDone := true.B }
            .elsewhen(kind === 1.U) { parseIndex := parseIndex + 1.U }
            .elsewhen((parseIndex +& 2.U) > headerLength || (parseIndex +& 2.U) > count ||
                optionLength < 2.U || end > headerLength || end > count) {
                parseDone := true.B; optionBad := true.B
            }.otherwise {
                parseIndex := end(5, 0)
                switch(kind) {
                    is(2.U) {
                        when(optionLength =/= 4.U) { optionBad := true.B }
                        when(optionLength === 4.U && syn && !options.mssPresent) {
                            options.mssPresent := true.B; options.mss := optionField(2, 2)
                        }
                    }
                    is(3.U) {
                        when(optionLength =/= 3.U) { optionBad := true.B }
                        when(optionLength === 3.U && syn && !options.scalePresent) {
                            options.scalePresent := true.B; options.scale := Mux(optionField(2, 1) > 14.U, 14.U, optionField(2, 1))
                        }
                    }
                    is(4.U) {
                        when(optionLength =/= 2.U) { optionBad := true.B }
                        when(optionLength === 2.U && syn) { options.sackPermitted := true.B }
                    }
                    is(5.U) {
                        val legal = optionLength === 10.U || optionLength === 18.U || optionLength === 26.U || optionLength === 34.U
                        when(!legal) { optionBad := true.B }
                        when(legal && options.sackCount === 0.U) {
                            options.sackCount := (optionLength - 2.U) >> 3
                            for (i <- 0 until 4) {
                                when(optionLength >= (10 + i * 8).U) {
                                    options.sackLeft(i) := optionField(2 + i * 8, 4)
                                    options.sackRight(i) := optionField(6 + i * 8, 4)
                                }
                            }
                        }
                    }
                    is(8.U) {
                        when(optionLength =/= 10.U) { optionBad := true.B }
                        when(optionLength === 10.U && !options.timestampPresent) {
                            options.timestampPresent := true.B
                            options.timestamp := optionField(2, 4); options.timestampEcho := optionField(6, 4)
                        }
                    }
                }
            }
    }
    io.out.valid := waiting && parseDone && checksum.io.out.valid
    io.out.bits.sourceIp := source; io.out.bits.destinationIp := destination
    io.out.bits.sourcePort := field(0, 2); io.out.bits.destinationPort := field(2, 2)
    io.out.bits.sequence := field(4, 4); io.out.bits.acknowledgement := field(8, 4)
    io.out.bits.flags := byte(13); io.out.bits.window := field(14, 2); io.out.bits.urgent := field(18, 2)
    io.out.bits.headerLength := headerLength
    io.out.bits.payloadLength := Mux(length >= headerLength, length - headerLength, 0.U)
    io.out.bits.options := options
    io.out.bits.errors := Cat(optionBad, checksum.io.out.bits.checksum =/= 0.U, lengthBad,
        streamBad || checksum.io.out.bits.error)
    checksum.io.out.ready := waiting && parseDone && io.out.ready
    when(io.out.fire) {
        waiting := false.B; active := false.B; count := 0.U; wordIndex := 0.U
        streamBad := false.B; lengthBad := false.B; headerReady := false.B
        parseDone := false.B; optionBad := false.B; parseIndex := 20.U
        options := 0.U.asTypeOf(new TcpOptions)
    }
}

/** Validated segment metadata followed by header-stripped payload. A header-only
  * segment still produces a single keep=0,last beat. The connection engine must
  * consume/drain this stream after every metadata event, even when rejecting a
  * sequence number. Wire validation alone does not authorize acknowledging data.
  */
class TcpReceive(val beatsPerPacket: Int = 1024, val tagged: Boolean = false) extends Module {
    val io = IO(new Bundle {
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val sourceIp = Input(UInt(32.W)); val destinationIp = Input(UInt(32.W)); val ipPayloadLength = Input(UInt(16.W))
        val metadata = Decoupled(new TcpSegment)
        val payload = Decoupled(new PacketBeat(16))
        val dropped = Output(Bool())
        val inContext = if (tagged) Some(Input(UInt(64.W))) else None
        val metadataContext = if (tagged) Some(Output(UInt(64.W))) else None
        val droppedContext = if (tagged) Some(Output(UInt(64.W))) else None
    })
    val validator = Module(new TcpValidate)
    val buffer = Module(new PacketCommitBuffer(16, beatsPerPacket))
    val metadata = Module(new MemoryQueue(new TcpSegment, 2))
    val slice = Module(new PacketSlice)
    validator.io.sourceIp := io.sourceIp; validator.io.destinationIp := io.destinationIp
    validator.io.ipPayloadLength := io.ipPayloadLength
    buffer.io.inContext := (if (tagged) io.inContext.get else 0.U)
    io.in.ready := validator.io.in.ready && buffer.io.in.ready
    validator.io.in.valid := io.in.valid && buffer.io.in.ready
    buffer.io.in.valid := io.in.valid && validator.io.in.ready
    validator.io.in.bits := io.in.bits; buffer.io.in.bits := io.in.bits
    buffer.io.verdict.valid := validator.io.out.valid && metadata.io.enq.ready
    buffer.io.verdict.bits := validator.io.out.bits.errors === 0.U
    validator.io.out.ready := buffer.io.verdict.ready && metadata.io.enq.ready
    metadata.io.enq.valid := validator.io.out.valid && buffer.io.verdict.ready &&
        validator.io.out.bits.errors === 0.U && !buffer.io.dropped
    metadata.io.enq.bits := validator.io.out.bits
    io.metadata.valid := metadata.io.deq.valid && slice.io.config.ready
    io.metadata.bits := metadata.io.deq.bits
    slice.io.config.valid := metadata.io.deq.valid && io.metadata.ready
    metadata.io.deq.ready := slice.io.config.ready && io.metadata.ready
    slice.io.config.bits.skip := metadata.io.deq.bits.headerLength
    slice.io.config.bits.length := metadata.io.deq.bits.payloadLength
    slice.io.config.bits.context := 0.U
    slice.io.in <> buffer.io.out
    io.payload <> slice.io.out
    io.dropped := buffer.io.dropped
    if (tagged) {
        io.metadataContext.get := buffer.io.outContext
        io.droppedContext.get := buffer.io.droppedContext
    }
}

class TcpReceiveHarness extends Module {
    val io = IO(new Bundle {
        val lo = Input(UInt(64.W)); val hi = Input(UInt(64.W)); val keep = Input(UInt(16.W))
        val last = Input(Bool()); val error = Input(Bool()); val valid = Input(Bool()); val ready = Output(Bool())
        val sourceIp = Input(UInt(32.W)); val destinationIp = Input(UInt(32.W)); val ipLength = Input(UInt(16.W))
        val metaReady = Input(Bool()); val metaValid = Output(Bool())
        val srcIp = Output(UInt(32.W)); val dstIp = Output(UInt(32.W))
        val srcPort = Output(UInt(16.W)); val dstPort = Output(UInt(16.W))
        val sequence = Output(UInt(32.W)); val acknowledgement = Output(UInt(32.W))
        val flags = Output(UInt(8.W)); val window = Output(UInt(16.W)); val urgent = Output(UInt(16.W))
        val headerLength = Output(UInt(6.W)); val payloadLength = Output(UInt(16.W))
        val optionFlags = Output(UInt(4.W)); val mss = Output(UInt(16.W)); val scale = Output(UInt(4.W))
        val timestamp = Output(UInt(32.W)); val timestampEcho = Output(UInt(32.W)); val sackCount = Output(UInt(3.W))
        val sackLeftLo = Output(UInt(64.W)); val sackLeftHi = Output(UInt(64.W))
        val sackRightLo = Output(UInt(64.W)); val sackRightHi = Output(UInt(64.W))
        val outReady = Input(Bool()); val outValid = Output(Bool())
        val outLo = Output(UInt(64.W)); val outHi = Output(UInt(64.W)); val outKeep = Output(UInt(16.W))
        val outLast = Output(Bool()); val outError = Output(Bool()); val dropped = Output(Bool())
    })
    val dut = Module(new TcpReceive(32))
    dut.io.in.valid := io.valid; io.ready := dut.io.in.ready
    dut.io.in.bits.data := Cat(io.hi, io.lo); dut.io.in.bits.keep := io.keep
    dut.io.in.bits.last := io.last; dut.io.in.bits.error := io.error
    dut.io.sourceIp := io.sourceIp; dut.io.destinationIp := io.destinationIp; dut.io.ipPayloadLength := io.ipLength
    dut.io.metadata.ready := io.metaReady; io.metaValid := dut.io.metadata.valid
    val meta = dut.io.metadata.bits
    io.srcIp := meta.sourceIp; io.dstIp := meta.destinationIp
    io.srcPort := meta.sourcePort; io.dstPort := meta.destinationPort
    io.sequence := meta.sequence; io.acknowledgement := meta.acknowledgement
    io.flags := meta.flags; io.window := meta.window; io.urgent := meta.urgent
    io.headerLength := meta.headerLength; io.payloadLength := meta.payloadLength
    io.optionFlags := Cat(meta.options.timestampPresent, meta.options.sackPermitted, meta.options.scalePresent, meta.options.mssPresent)
    io.mss := meta.options.mss; io.scale := meta.options.scale
    io.timestamp := meta.options.timestamp; io.timestampEcho := meta.options.timestampEcho; io.sackCount := meta.options.sackCount
    io.sackLeftLo := Cat(meta.options.sackLeft(1), meta.options.sackLeft(0))
    io.sackLeftHi := Cat(meta.options.sackLeft(3), meta.options.sackLeft(2))
    io.sackRightLo := Cat(meta.options.sackRight(1), meta.options.sackRight(0))
    io.sackRightHi := Cat(meta.options.sackRight(3), meta.options.sackRight(2))
    dut.io.payload.ready := io.outReady; io.outValid := dut.io.payload.valid
    io.outLo := dut.io.payload.bits.data(63, 0); io.outHi := dut.io.payload.bits.data(127, 64)
    io.outKeep := dut.io.payload.bits.keep; io.outLast := dut.io.payload.bits.last; io.outError := dut.io.payload.bits.error
    io.dropped := dut.io.dropped
}
