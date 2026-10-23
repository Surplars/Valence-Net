package netstack

import chisel3._
import chisel3.util._

/** Byte zero is data[7:0], i.e. the earliest byte on the wire.
  * Keep must be a contiguous low-bit prefix; a zero-byte beat is legal.
  * Packets end only on a transferred last beat. Error poisons the whole packet.
  */
class PacketBeat(val bytes: Int) extends Bundle {
    val data = UInt((bytes * 8).W)
    val keep = UInt(bytes.W)
    val last = Bool()
    val error = Bool()
}

class ChecksumResult extends Bundle {
    val checksum = UInt(16.W)
    val error = Bool()
}

/** End-around carry-save reduction of one's-complement words.
  * Each 3:2 compressor wraps its top carry into bit zero: 2^16 == 1
  * modulo 65535. Only the final two operands use a carry-propagating adder.
  * Splitting wide operands retains their high-word carry and distinguishes
  * positive zero (all inputs zero) from negative zero (a positive multiple
  * of 65535), as required by Internet checksum generation.
  */
object OnesComplement {
    def sum16(values: Seq[UInt]): UInt = {
        require(values.nonEmpty)
        val words = values.flatMap { value =>
            val width = value.getWidth
            require(width > 0, "checksum operands need known widths")
            (0 until width by 16).map { low =>
                value(math.min(width - 1, low + 15), low).pad(16)
            }
        }
        def compress(xs: Seq[UInt]): Seq[UInt] = if (xs.size <= 2) xs else {
            compress(xs.grouped(3).flatMap {
                case Seq(a, b, c) =>
                    val carry = (a & b) | (a & c) | (b & c)
                    Seq(a ^ b ^ c, Cat(carry(14, 0), carry(15)))
                case rest => rest
            }.toSeq)
        }
        val reduced = compress(words)
        if (reduced.size == 1) reduced.head else {
            val total = reduced.head +& reduced(1)
            // A two-word sum is at most 0x1fffe; this fold cannot overflow.
            total(15, 0) + total(16)
        }
    }
}

/** RFC 1071 one's-complement sum with an uncomplemented 16-bit seed.
  * Seed is sampled with the first beat (e.g. a folded IPv4 pseudo-header).
  * Partial beats are accepted even in the middle of a packet: odd byte parity
  * is carried across beats. A consumer can use checksum==0 for validation.
  * UDP generation must separately map checksum zero to 0xffff (RFC 768).
  */
class InternetChecksum(val bytes: Int = 16) extends Module {
    require(bytes >= 2 && bytes <= 64 && isPow2(bytes))
    val io = IO(new Bundle {
        val in = Flipped(Decoupled(new PacketBeat(bytes)))
        val seed = Input(UInt(16.W))
        val out = Decoupled(new ChecksumResult)
    })
    val active = RegInit(false.B)
    val odd = RegInit(false.B)
    val sum = RegInit(0.U(16.W))
    val bad = RegInit(false.B)
    val result = Reg(new ChecksumResult)
    val pending = RegInit(false.B)

    io.out.valid := pending
    io.out.bits := result
    io.in.ready := !pending || io.out.ready

    val parity = active && odd
    val words = (0 until bytes / 2).map { pair =>
        val a = Mux(io.in.bits.keep(2 * pair), io.in.bits.data(16 * pair + 7, 16 * pair), 0.U(8.W))
        val b = Mux(io.in.bits.keep(2 * pair + 1), io.in.bits.data(16 * pair + 15, 16 * pair + 8), 0.U(8.W))
        Mux(parity, Cat(b, a), Cat(a, b))
    }
    val folded = OnesComplement.sum16(words :+ Mux(active, sum, io.seed))
    val legal = (io.in.bits.keep & (io.in.bits.keep + 1.U)) === 0.U
    val error = (active && bad) || !legal || io.in.bits.error

    when(io.out.fire) { pending := false.B }
    when(io.in.fire) {
        sum := folded
        odd := parity ^ PopCount(io.in.bits.keep)(0)
        bad := error
        active := !io.in.bits.last
        when(io.in.bits.last) {
            result.checksum := ~folded
            result.error := error
            pending := true.B
        }
    }
}

/** Narrow host-language accessors for the 128-bit test stream. */
class ChecksumHarness extends Module {
    val io = IO(new Bundle {
        val lo = Input(UInt(64.W))
        val hi = Input(UInt(64.W))
        val keep = Input(UInt(16.W))
        val last = Input(Bool())
        val error = Input(Bool())
        val seed = Input(UInt(16.W))
        val valid = Input(Bool())
        val ready = Output(Bool())
        val outReady = Input(Bool())
        val outValid = Output(Bool())
        val checksum = Output(UInt(16.W))
        val bad = Output(Bool())
    })
    val dut = Module(new InternetChecksum(16))
    dut.io.in.bits.data := Cat(io.hi, io.lo)
    dut.io.in.bits.keep := io.keep
    dut.io.in.bits.last := io.last
    dut.io.in.bits.error := io.error
    dut.io.in.valid := io.valid
    dut.io.seed := io.seed
    io.ready := dut.io.in.ready
    dut.io.out.ready := io.outReady
    io.outValid := dut.io.out.valid
    io.checksum := dut.io.out.bits.checksum
    io.bad := dut.io.out.bits.error
}
