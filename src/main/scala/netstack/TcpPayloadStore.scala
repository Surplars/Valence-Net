package netstack

import chisel3._
import chisel3.util._

class TcpStoreAllocation extends Bundle {
    val length = UInt(16.W)
    val context = UInt(64.W)
}
class TcpStoreResult extends Bundle {
    val token = UInt(32.W)
    val length = UInt(16.W)
    val context = UInt(64.W)
    val error = Bool()
}
class TcpStoreRead extends Bundle {
    val token = UInt(32.W)
    val offset = UInt(16.W)
    val length = UInt(16.W)
}

/** Retained TCP payload banks with replay and arbitrary byte-range reads.
  * A successful store result transfers ownership to the connection engine.
  * Reads never free data: only an explicit release (after validated ACK/reset)
  * does so. A release during replay retires the bank until the current output
  * finishes; it cannot overwrite a stalled retransmission. Generation-tagged
  * tokens reject stale reads/releases after bank reuse. All tokens are invalid
  * across hardware reset, which must also reset the engine's descriptors.
  * This store deliberately knows no TCP sequence numbers or ACK policy.
  */
class TcpPayloadStore(val banks: Int = 8, val beatsPerBank: Int = 128) extends Module {
    require(isPow2(banks) && banks >= 2 && banks <= 256)
    require(isPow2(beatsPerBank) && beatsPerBank >= 2 && beatsPerBank <= 2048)
    val bankBits = log2Ceil(banks)
    val wordBits = log2Ceil(beatsPerBank)
    val io = IO(new Bundle {
        val allocate = Flipped(Decoupled(new TcpStoreAllocation))
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val stored = Decoupled(new TcpStoreResult)
        val read = Flipped(Decoupled(new TcpStoreRead))
        val response = Decoupled(new TcpStoreResult)
        val out = Decoupled(new PacketBeat(16))
        val outToken = Output(UInt(32.W))
        val outContext = Output(UInt(64.W))
        val release = Flipped(Valid(UInt(32.W)))
        val releaseMiss = Output(Bool())
        val freeBanks = Output(UInt(log2Ceil(banks + 1).W))
    })
    val memory = SyncReadMem(banks * beatsPerBank, UInt(128.W))
    // 0 free, 1 writing/awaiting ownership transfer, 2 retained, 3 pinned release.
    val state = RegInit(VecInit(Seq.fill(banks)(0.U(2.W))))
    val generation = RegInit(VecInit(Seq.fill(banks)(0.U((32 - bankBits).W))))
    val lengths = Reg(Vec(banks, UInt(16.W)))
    val contexts = Reg(Vec(banks, UInt(64.W)))
    def token(bank: UInt): UInt = Cat(generation(bank), bank)
    val free = VecInit(state.map(_ === 0.U))
    val chosen = PriorityEncoder(free.asUInt)
    io.freeBanks := PopCount(free)

    val writing = RegInit(false.B)
    val writeBank = Reg(UInt(bankBits.W))
    val writeWord = RegInit(0.U((wordBits + 1).W))
    val writeCount = RegInit(0.U(17.W))
    val bad = RegInit(false.B)
    val storedPending = RegInit(false.B)
    val stored = Reg(new TcpStoreResult)
    io.allocate.ready := !writing && !storedPending && free.asUInt.orR
    io.in.ready := writing
    io.stored.valid := storedPending
    io.stored.bits := stored
    when(io.stored.fire) {
        storedPending := false.B
        when(!stored.error) { state(stored.token(bankBits - 1, 0)) := 2.U }
    }
    when(io.allocate.fire) {
        writing := true.B
        writeBank := chosen
        writeWord := 0.U
        writeCount := 0.U
        bad := io.allocate.bits.length > (beatsPerBank * 16).U
        state(chosen) := 1.U
        generation(chosen) := generation(chosen) + 1.U
        lengths(chosen) := io.allocate.bits.length
        contexts(chosen) := io.allocate.bits.context
    }
    val nextCount = writeCount +& PopCount(io.in.bits.keep)
    val legal = (io.in.bits.keep & (io.in.bits.keep + 1.U)) === 0.U
    val beatBad = io.in.bits.error || !legal ||
        (!io.in.bits.last && io.in.bits.keep =/= "hffff".U) || writeWord >= beatsPerBank.U
    when(io.in.fire) {
        when(writeWord < beatsPerBank.U) {
            memory.write(Cat(writeBank, writeWord(wordBits - 1, 0)), io.in.bits.data)
            writeWord := writeWord + 1.U
        }
        writeCount := Mux(nextCount > 131071.U, 131071.U, nextCount)
        bad := bad || beatBad
        when(io.in.bits.last) {
            val rejected = bad || beatBad || nextCount =/= lengths(writeBank)
            writing := false.B
            state(writeBank) := Mux(rejected, 0.U, 1.U)
            storedPending := true.B
            stored.token := token(writeBank)
            stored.length := lengths(writeBank)
            stored.context := contexts(writeBank)
            stored.error := rejected
        }
    }

    val reading = RegInit(false.B)
    val validating = RegInit(false.B)
    val requestOwned = RegInit(false.B)
    val requestEnd = Reg(UInt(17.W))
    val requestCapacity = Reg(UInt(16.W))
    val requestContext = Reg(UInt(64.W))
    val streaming = RegInit(false.B)
    val readBank = Reg(UInt(bankBits.W))
    val readWord = Reg(UInt(wordBits.W))
    val lastWord = Reg(UInt(wordBits.W))
    val lastKeep = Reg(UInt(16.W))
    val readOffset = Reg(UInt(4.W))
    val empty = RegInit(false.B)
    val issuedLast = RegInit(false.B)
    val responsePending = RegInit(false.B)
    val response = Reg(new TcpStoreResult)
    val slice = Module(new PacketSlice)
    val output = Module(new Queue(new PacketBeat(16), 2))
    val pending = RegInit(false.B)
    val pendingLast = Reg(Bool())
    val room = (output.io.count +& pending.asUInt) < 2.U || output.io.deq.fire
    val issue = reading && streaming && !empty && !issuedLast && room
    val readData = memory.read(Cat(readBank, readWord), issue)
    pending := issue
    when(issue) {
        pendingLast := readWord === lastWord
        readWord := readWord + 1.U
        when(readWord === lastWord) { issuedLast := true.B }
    }
    output.io.enq.valid := pending
    output.io.enq.bits.data := readData
    output.io.enq.bits.keep := Mux(pendingLast, lastKeep, "hffff".U)
    output.io.enq.bits.last := pendingLast
    output.io.enq.bits.error := false.B
    slice.io.in.valid := Mux(empty, streaming && !issuedLast, output.io.deq.valid)
    slice.io.in.bits := output.io.deq.bits
    when(empty) { slice.io.in.bits := 0.U.asTypeOf(new PacketBeat(16)); slice.io.in.bits.last := true.B }
    output.io.deq.ready := slice.io.in.ready && !empty
    when(empty && slice.io.in.fire) { issuedLast := true.B }
    io.out <> slice.io.out
    io.outToken := slice.io.context(95, 64)
    io.outContext := slice.io.context(63, 0)

    io.response.valid := responsePending && (response.error || slice.io.config.ready)
    io.response.bits := response
    slice.io.config.valid := responsePending && !response.error && io.response.ready
    slice.io.config.bits.skip := Mux(empty, 0.U, readOffset)
    slice.io.config.bits.length := response.length
    slice.io.config.bits.context := Cat(response.token, response.context)
    when(io.response.fire) {
        responsePending := false.B
        when(!response.error) { streaming := true.B }
    }

    val releaseBank = io.release.bits(bankBits - 1, 0)
    val releaseHit = state(releaseBank) === 2.U && token(releaseBank) === io.release.bits
    io.releaseMiss := io.release.valid && !releaseHit
    val requestedBank = io.read.bits.token(bankBits - 1, 0)
    val end = io.read.bits.offset +& io.read.bits.length
    val ownerGood = state(requestedBank) === 2.U && token(requestedBank) === io.read.bits.token &&
        !(io.release.valid && io.release.bits === io.read.bits.token)
    val rangeGood = requestEnd <= requestCapacity
    val readGood = requestOwned && rangeGood
    io.read.ready := !reading && !validating && !responsePending
    when(io.read.fire) {
        // Capture the complete request before range/address arithmetic. The
        // ownership check is independent of the late MSS/segment length.
        validating := true.B
        requestOwned := ownerGood
        requestEnd := end
        requestCapacity := lengths(requestedBank)
        requestContext := contexts(requestedBank)
        response.token := io.read.bits.token
        response.length := io.read.bits.length
        streaming := false.B
        readBank := requestedBank
        readWord := (io.read.bits.offset >> 4)(wordBits - 1, 0)
        readOffset := io.read.bits.offset(3, 0)
        empty := io.read.bits.length === 0.U
        issuedLast := false.B
    }
    when(validating) {
        validating := false.B
        responsePending := true.B
        response.context := Mux(readGood, requestContext, 0.U)
        response.error := !readGood
        reading := readGood
        lastWord := ((requestEnd - 1.U) >> 4)(wordBits - 1, 0)
        lastKeep := Mux(requestEnd(3, 0) === 0.U, "hffff".U,
            ((1.U(17.W) << requestEnd(3, 0)) - 1.U)(15, 0))
    }
    when(io.release.valid && releaseHit) {
        // A valid request already owns its bank during validation. Invalid
        // ranges never pin a release, and rejected reads issue no RAM data.
        val pinned = reading || (validating && readGood)
        state(releaseBank) := Mux(pinned && readBank === releaseBank, 3.U, 0.U)
    }
    when(io.out.fire && io.out.bits.last) {
        reading := false.B
        streaming := false.B
        when(state(readBank) === 3.U || (io.release.valid && releaseHit && releaseBank === readBank)) {
            state(readBank) := 0.U
        }
    }
}

class TcpStoreHarness extends Module {
    val io = IO(new Bundle {
        val allocateValid = Input(Bool()); val allocateReady = Output(Bool())
        val length = Input(UInt(16.W)); val context = Input(UInt(64.W))
        val valid = Input(Bool()); val ready = Output(Bool())
        val lo = Input(UInt(64.W)); val hi = Input(UInt(64.W)); val keep = Input(UInt(16.W))
        val last = Input(Bool()); val error = Input(Bool())
        val storedValid = Output(Bool()); val storedReady = Input(Bool())
        val storedToken = Output(UInt(32.W)); val storedLength = Output(UInt(16.W))
        val storedContext = Output(UInt(64.W)); val storedError = Output(Bool())
        val readValid = Input(Bool()); val readReady = Output(Bool())
        val readToken = Input(UInt(32.W)); val readOffset = Input(UInt(16.W)); val readLength = Input(UInt(16.W))
        val responseValid = Output(Bool()); val responseReady = Input(Bool())
        val responseToken = Output(UInt(32.W)); val responseLength = Output(UInt(16.W))
        val responseContext = Output(UInt(64.W)); val responseError = Output(Bool())
        val outValid = Output(Bool()); val outReady = Input(Bool())
        val outLo = Output(UInt(64.W)); val outHi = Output(UInt(64.W)); val outKeep = Output(UInt(16.W))
        val outLast = Output(Bool()); val outError = Output(Bool())
        val outToken = Output(UInt(32.W)); val outContext = Output(UInt(64.W))
        val releaseValid = Input(Bool()); val releaseToken = Input(UInt(32.W)); val releaseMiss = Output(Bool())
        val freeBanks = Output(UInt(3.W))
    })
    val dut = Module(new TcpPayloadStore(4, 16))
    dut.io.allocate.valid := io.allocateValid; io.allocateReady := dut.io.allocate.ready
    dut.io.allocate.bits.length := io.length; dut.io.allocate.bits.context := io.context
    dut.io.in.valid := io.valid; io.ready := dut.io.in.ready
    dut.io.in.bits.data := Cat(io.hi, io.lo); dut.io.in.bits.keep := io.keep
    dut.io.in.bits.last := io.last; dut.io.in.bits.error := io.error
    io.storedValid := dut.io.stored.valid; dut.io.stored.ready := io.storedReady
    io.storedToken := dut.io.stored.bits.token; io.storedLength := dut.io.stored.bits.length
    io.storedContext := dut.io.stored.bits.context; io.storedError := dut.io.stored.bits.error
    dut.io.read.valid := io.readValid; io.readReady := dut.io.read.ready
    dut.io.read.bits.token := io.readToken; dut.io.read.bits.offset := io.readOffset; dut.io.read.bits.length := io.readLength
    io.responseValid := dut.io.response.valid; dut.io.response.ready := io.responseReady
    io.responseToken := dut.io.response.bits.token; io.responseLength := dut.io.response.bits.length
    io.responseContext := dut.io.response.bits.context; io.responseError := dut.io.response.bits.error
    io.outValid := dut.io.out.valid; dut.io.out.ready := io.outReady
    io.outLo := dut.io.out.bits.data(63, 0); io.outHi := dut.io.out.bits.data(127, 64)
    io.outKeep := dut.io.out.bits.keep; io.outLast := dut.io.out.bits.last; io.outError := dut.io.out.bits.error
    io.outToken := dut.io.outToken; io.outContext := dut.io.outContext
    dut.io.release.valid := io.releaseValid; dut.io.release.bits := io.releaseToken
    io.releaseMiss := dut.io.releaseMiss; io.freeBanks := dut.io.freeBanks
}
