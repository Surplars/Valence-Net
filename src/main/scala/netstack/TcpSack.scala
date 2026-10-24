package netstack

import chisel3._
import chisel3.util._

class TcpSackBlocks extends Bundle {
    val count = UInt(3.W)
    val left = Vec(4, UInt(32.W))
    val right = Vec(4, UInt(32.W))
}
object TcpSackEncoding {
    def minimum(xs: Seq[UInt]): UInt = if (xs.size == 1) xs.head else minimum(xs.grouped(2).map {
        case Seq(a, b) => Mux(a < b, a, b)
        case Seq(a) => a
    }.toSeq)
    def after(a: UInt, b: UInt): Bool = { val d = (a - b)(31, 0); d =/= 0.U && !d(31) }
    // No timestamps are generated, leaving room for four blocks (36 padded bytes).
    def apply(s: TcpSackBlocks, ack: UInt, enabled: Bool): (UInt, UInt) = {
        val keep = (0 until 4).map(i => enabled && i.U < s.count && after(s.left(i), ack) && after(s.right(i), s.left(i)))
        val count = PopCount(keep)
        val bytes = Wire(Vec(40, UInt(8.W))); bytes.foreach(_ := 0.U)
        bytes(0) := 1.U; bytes(1) := 1.U; bytes(2) := 5.U; bytes(3) := 2.U + (count << 3)
        for (i <- 0 until 4) {
            val target = if (i == 0) 0.U else PopCount(keep.take(i))
            for (j <- 0 until 4) when(keep(i) && target === j.U) {
                for (b <- 0 until 4) {
                    bytes(4 + j * 8 + b) := s.left(i)(31 - b * 8, 24 - b * 8)
                    bytes(8 + j * 8 + b) := s.right(i)(31 - b * 8, 24 - b * 8)
                }
            }
        }
        (Mux(count === 0.U, 0.U, 4.U + (count << 3)), bytes.asUInt)
    }
}

/** Cold-path SACK snapshot. Two interleaved single-write RAMs hold the sort
  * scratch space: adjacent entries always occupy different banks. Capture is
  * atomic; loading, compare/exchange and merging never expose a partial result.
  * A shortened bubble pass and streaming top-four insertion keep this cold
  * work bounded without a multiported register-file write/multiplexer network.
  */
class TcpSackSnapshot(val banks: Int = 8) extends Module {
    require(isPow2(banks) && banks >= 2 && banks <= 256)
    val ageBits = log2Ceil(banks + 1)
    val io = IO(new Bundle {
        val valid = Input(Vec(banks, Bool()))
        val left = Input(Vec(banks, UInt(32.W))); val right = Input(Vec(banks, UInt(32.W)))
        val age = Input(Vec(banks, UInt(ageBits.W)))
        val base = Input(UInt(32.W)); val changed = Input(Bool()); val clear = Input(Bool())
        val blocks = Output(new TcpSackBlocks); val updated = Output(Bool()); val busy = Output(Bool())
    })
    val bits = log2Ceil(banks)
    val pairBits = math.max(1, bits - 1)
    val width = 65 + ageBits
    def leftOf(word: UInt): UInt = word(31, 0)
    def rightOf(word: UInt): UInt = word(63, 32)
    def ageOf(word: UInt): UInt = word(63 + ageBits, 64)
    def validOf(word: UInt): Bool = word(width - 1)
    def normalized(word: UInt, base: UInt): UInt = Cat(validOf(word), ageOf(word),
        (rightOf(word) - base)(31, 0), (leftOf(word) - base)(31, 0))
    def address(index: UInt): UInt = if (banks == 2) 0.U(pairBits.W) else index(bits - 1, 1)

    val idle :: loading :: sorting :: merging :: flushing :: publish :: Nil = Enum(6)
    val state = RegInit(idle); val dirty = RegInit(false.B)
    val available = RegInit(false.B)
    // Snapshot inputs must remain coherent even if ACK progress or a new
    // committed range changes the live descriptor table during RAM loading.
    val captured = Reg(Vec(banks, UInt(width.W)))
    val base = Reg(UInt(32.W))
    val load = Reg(UInt(pairBits.W))
    val pos = Reg(UInt(bits.W)); val pass = Reg(UInt(bits.W))
    val even = Mem(banks / 2, UInt(width.W))
    val odd = Mem(banks / 2, UInt(width.W))
    val evenAddress = (address(pos) + Mux(state === sorting, pos(0), false.B).asUInt)(pairBits - 1, 0)
    val oddAddress = address(pos)
    val evenWord = even(evenAddress); val oddWord = odd(oddAddress)
    val a = Mux(pos(0), oddWord, evenWord)
    val b = Mux(pos(0), evenWord, oddWord)
    val exchange = state === sorting && validOf(b) &&
        (!validOf(a) || leftOf(b) < leftOf(a))
    val evens = VecInit((0 until banks / 2).map(i => captured(2 * i)))
    val odds = VecInit((0 until banks / 2).map(i => captured(2 * i + 1)))
    // Exactly one write port per RAM, including snapshot load and swaps.
    when(state === loading || exchange) {
        even.write(Mux(state === loading, load, evenAddress),
            Mux(state === loading, normalized(evens(load), base), oddWord))
        odd.write(Mux(state === loading, load, oddAddress),
            Mux(state === loading, normalized(odds(load), base), evenWord))
    }
    val groupValid = RegInit(false.B)
    val groupLeft = Reg(UInt(32.W)); val groupRight = Reg(UInt(32.W))
    val groupAge = Reg(UInt(ageBits.W))
    val bestLeft = Reg(Vec(4, UInt(32.W))); val bestRight = Reg(Vec(4, UInt(32.W)))
    val bestAge = Reg(Vec(4, UInt(ageBits.W)))
    val adjacent = groupValid && groupRight === leftOf(a)
    val finishGroup = (state === merging && validOf(a) && groupValid && !adjacent) ||
        (state === flushing && groupValid)
    val insert = VecInit((0 until 4).map(i => groupAge < bestAge(i)))
    val insertAt = PriorityEncoder(insert.asUInt)
    when(finishGroup && insert.asUInt.orR) {
        for (i <- 0 until 4) {
            when(insertAt <= i.U) {
                if (i == 0) {
                    bestLeft(i) := groupLeft; bestRight(i) := groupRight; bestAge(i) := groupAge
                } else {
                    bestLeft(i) := Mux(insertAt === i.U, groupLeft, bestLeft(i - 1))
                    bestRight(i) := Mux(insertAt === i.U, groupRight, bestRight(i - 1))
                    bestAge(i) := Mux(insertAt === i.U, groupAge, bestAge(i - 1))
                }
            }
        }
    }
    val published = RegInit(0.U.asTypeOf(new TcpSackBlocks))
    io.busy := state =/= idle || dirty || io.changed
    io.blocks := published
    when(!available || io.changed) { io.blocks.count := 0.U }
    io.updated := false.B
    when(io.changed) { dirty := true.B; available := false.B }
    when(state === idle && (dirty || io.changed)) {
        for (i <- 0 until banks) { captured(i) := Cat(io.valid(i), io.age(i), io.right(i), io.left(i)) }
        base := io.base; load := 0.U; state := loading; dirty := false.B
        groupValid := false.B; bestAge.foreach(_ := banks.U)
    }
    when(state === loading) {
        when(load === (banks / 2 - 1).U) { pos := 0.U; pass := 0.U; state := sorting }
            .otherwise { load := load + 1.U }
    }
    when(state === sorting) {
        // Each completed pass fixes the largest remaining entry at the end.
        when(pos === ((banks - 2).U - pass)) {
            pos := 0.U
            when(pass === (banks - 2).U) { state := merging }
                .otherwise { pass := pass + 1.U }
        }.otherwise { pos := pos + 1.U }
    }
    when(state === merging) {
        when(validOf(a)) {
            when(adjacent) {
                groupRight := rightOf(a)
                groupAge := Mux(groupAge < ageOf(a), groupAge, ageOf(a))
            }.otherwise {
                groupValid := true.B; groupLeft := leftOf(a); groupRight := rightOf(a); groupAge := ageOf(a)
            }
        }
        when(pos === (banks - 1).U) { state := flushing }
            .otherwise { pos := pos + 1.U }
    }
    when(state === flushing) { state := publish }
    when(state === publish) {
        state := idle
        when(!dirty && !io.changed) {
            published.count := PopCount(bestAge.map(_ < banks.U))
            for (i <- 0 until 4) {
                published.left(i) := Mux(bestAge(i) < banks.U, bestLeft(i) + base, 0.U)
                published.right(i) := Mux(bestAge(i) < banks.U, bestRight(i) + base, 0.U)
            }
            available := true.B; io.updated := true.B
        }
    }
    when(io.clear) {
        state := idle; dirty := false.B; available := false.B; published.count := 0.U
    }
}

/** Bounded advisory scoreboard plus ACK-clocked selective replay.
  * Sixteen ranges remember up to four most recent SACK-bearing ACKs. Replacing
  * old information may cause extra retransmission; it cannot release data.
  * Reno's three duplicate ACKs start recovery. At most one additional MSS is
  * permitted per subsequent validated duplicate/progress ACK; no rescue burst
  * or RFC6675 Pipe/IsLost implementation is claimed. RTO forgets every block.
  */
class TcpSackRecovery extends Module {
    val io = IO(new Bundle {
        val enabled = Input(Bool()); val clear = Input(Bool()); val timeout = Input(Bool())
        val generation = Input(UInt(32.W)); val oldest = Input(UInt(32.W)); val actual = Input(UInt(32.W))
        val ack = Flipped(Valid(new TcpAckUpdate))
        val request = Flipped(Decoupled(UInt(32.W)))
        val replay = Decoupled(UInt(32.W)); val limit = Output(UInt(16.W))
        val prepared = Flipped(Valid(new TcpPayloadSegment)); val miss = Input(Bool())
    })
    def after(a: UInt, b: UInt): Bool = TcpSackEncoding.after(a, b)
    val valid = RegInit(VecInit(Seq.fill(16)(false.B)))
    val left = Reg(Vec(16, UInt(32.W))); val right = Reg(Vec(16, UInt(32.W)))
    val group = RegInit(0.U(2.W))
    val active = RegInit(false.B); val recoveryEnd = Reg(UInt(32.W))
    val cursor = Reg(UInt(32.W)); val credits = RegInit(0.U(4.W))
    val idle :: scan :: reduce :: resolve :: offer :: waiting :: Nil = Enum(6)
    val state = RegInit(idle)
    val cap = Reg(UInt(16.W))
    val ack = io.ack.bits
    val fresh = io.ack.valid && ack.generation === io.generation && !ack.handshake
    val advance = fresh && (ack.duplicate || after(ack.acknowledgement, ack.previous))
    when(fresh && ack.sack.count =/= 0.U && io.enabled && !io.timeout) {
        for (i <- 0 until 4) {
            val slot = Cat(group, i.U(2.W))
            val l = ack.sack.left(i); val r = ack.sack.right(i)
            val good = i.U < ack.sack.count && after(l, ack.acknowledgement) &&
                after(r, l) && !after(r, io.actual) && after(r, io.oldest) &&
                !((r - ack.acknowledgement)(31))
            valid(slot) := good; left(slot) := l; right(slot) := r
        }
        group := group + 1.U
    }
    for (i <- 0 until 16) when(valid(i) && !after(right(i), io.oldest)) { valid(i) := false.B }
    io.request.ready := state === idle && !io.timeout
    when(io.request.fire) {
        when(!active) {
            active := true.B; recoveryEnd := io.actual; cursor := io.request.bits; credits := 1.U
        }
    }
    when(active && advance && credits < 8.U) { credits := credits + 1.U }
    when(state === idle && active && credits =/= 0.U) {
        cursor := Mux(after(io.oldest, cursor), io.oldest, cursor); state := scan
    }
    val covered = VecInit((0 until 16).map(i => valid(i) && !after(left(i), cursor) && after(right(i), cursor)))
    val following = (0 until 16).map(i => Mux(valid(i) && after(left(i), cursor),
        (left(i) - cursor)(31, 0), "h7fffffff".U(32.W)))
    // Snapshot comparisons and range ends together: an ACK may replace a
    // scoreboard slot while this lookup is in flight. Mixing old hit bits
    // with a newer slot's right edge could otherwise skip an unsacked hole.
    val hits = Reg(Vec(16, Bool())); val hitEnds = Reg(Vec(16, UInt(32.W)))
    val distances = Reg(Vec(16, UInt(32.W))); val remaining = Reg(UInt(32.W))
    val minima = Reg(Vec(4, UInt(32.W)))
    val skip = Reg(Bool()); val skipEnd = Reg(UInt(32.W))
    when(state === scan) {
        when(after(io.oldest, cursor)) { cursor := io.oldest }
        .elsewhen(!after(recoveryEnd, cursor)) { state := idle; credits := 0.U }
        .otherwise {
            hits := covered; hitEnds := right; distances := VecInit(following)
            remaining := recoveryEnd - cursor; state := reduce
        }
    }
    when(state === reduce) {
        for (i <- 0 until 4) { minima(i) := TcpSackEncoding.minimum((0 until 4).map(j => distances(4 * i + j))) }
        skip := hits.asUInt.orR; skipEnd := hitEnds(PriorityEncoder(hits.asUInt)); state := resolve
    }
    when(state === resolve) {
        when(skip) { cursor := skipEnd; state := scan }
        .otherwise {
            val distance = TcpSackEncoding.minimum(minima.toSeq)
            val length = Mux(distance < remaining, distance, remaining)
            cap := Mux(length > 65535.U, 65535.U, length); state := offer
        }
    }
    io.replay.valid := state === offer && !io.clear && !io.timeout && io.enabled
    io.replay.bits := cursor; io.limit := cap
    when(io.replay.fire) {
        credits := Mux(advance && credits < 8.U, credits, credits - 1.U)
        state := Mux(io.miss, idle, waiting)
    }
    when(state === waiting && io.prepared.valid && io.prepared.bits.retransmission &&
        io.prepared.bits.generation === io.generation) {
        cursor := io.prepared.bits.sequence + io.prepared.bits.length; state := idle
    }
    when(active && !after(recoveryEnd, io.oldest)) { active := false.B; credits := 0.U; state := idle }
    when(io.clear || io.timeout || !io.enabled) {
        valid.foreach(_ := false.B); group := 0.U; active := false.B; credits := 0.U; state := idle
    }
}
