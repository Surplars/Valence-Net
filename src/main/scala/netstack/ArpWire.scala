package netstack

import chisel3._
import chisel3.util._

class ArpPacket extends Bundle {
    val operation = UInt(16.W)
    val senderMac = UInt(48.W)
    val senderIp = UInt(32.W)
    val targetMac = UInt(48.W)
    val targetIp = UInt(32.W)
}

/** Ethernet-II ARP decoder. MAC has removed FCS and reports frame errors in
  * error, including late FCS errors. Padding is drained before committing ARP.
  * Frames addressed to localMac or broadcast, Ethernet/IPv4 ARP only.
  */
class ArpFrameReceive extends Module {
    val io = IO(new Bundle {
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val localMac = Input(UInt(48.W))
        val out = Decoupled(new ArpPacket)
        val dropped = Output(Bool())
    })
    val header = Reg(Vec(3, UInt(128.W)))
    val beat = RegInit(0.U(2.W))
    val count = RegInit(0.U(17.W))
    val local = Reg(UInt(48.W))
    val bad = RegInit(false.B)
    val pending = RegInit(false.B)
    def byte(i: Int): UInt = header(i / 16)(8 * (i % 16) + 7, 8 * (i % 16))
    def field(i: Int, n: Int): UInt = Cat((i until i + n).map(byte))
    val fieldsValid = field(12, 2) === "h0806".U && field(14, 2) === 1.U &&
        field(16, 2) === "h0800".U && byte(18) === 6.U && byte(19) === 4.U &&
        (field(20, 2) === 1.U || field(20, 2) === 2.U) &&
        (field(0, 6) === local || field(0, 6) === "hffffffffffff".U) &&
        field(6, 6) === field(22, 6)
    io.in.ready := !pending
    io.out.valid := pending && !bad && fieldsValid
    io.out.bits.operation := field(20, 2)
    io.out.bits.senderMac := field(22, 6)
    io.out.bits.senderIp := field(28, 4)
    io.out.bits.targetMac := field(32, 6)
    io.out.bits.targetIp := field(38, 4)
    io.dropped := pending && (bad || !fieldsValid)
    val nextCount = count +& PopCount(io.in.bits.keep)
    val legalKeep = (io.in.bits.keep & (io.in.bits.keep + 1.U)) === 0.U
    when(io.in.fire) {
        when(beat === 0.U) { local := io.localMac }
        when(beat < 3.U) { header(beat) := io.in.bits.data; beat := beat + 1.U }
        count := Mux(nextCount > 131071.U, 131071.U, nextCount)
        bad := bad || io.in.bits.error || !legalKeep || io.in.bits.keep === 0.U ||
            (!io.in.bits.last && io.in.bits.keep =/= "hffff".U) || (io.in.bits.last && nextCount < 42.U)
        when(io.in.bits.last) { pending := true.B }
    }
    when(io.out.fire || io.dropped) { pending := false.B; bad := false.B; count := 0.U; beat := 0.U }
}

/** Full 60-byte Ethernet frame without preamble/FCS. Includes zero padding. */
class ArpFrameTransmit extends Module {
    val io = IO(new Bundle {
        val in = Flipped(Decoupled(new ArpPacket))
        val out = Decoupled(new PacketBeat(16))
        val complete = Output(Bool())
    })
    val packet = Reg(new ArpPacket)
    val busy = RegInit(false.B)
    val index = RegInit(0.U(2.W))
    val bytes = Wire(Vec(64, UInt(8.W)))
    bytes.foreach(_ := 0.U)
    def set(i: Int, n: Int, value: UInt): Unit = (0 until n).foreach { j =>
        bytes(i + j) := value((n - j) * 8 - 1, (n - j - 1) * 8)
    }
    set(0, 6, Mux(packet.operation === 1.U, "hffffffffffff".U(48.W), packet.targetMac))
    set(6, 6, packet.senderMac)
    set(12, 2, "h0806".U(16.W)); set(14, 2, 1.U(16.W)); set(16, 2, "h0800".U(16.W))
    bytes(18) := 6.U; bytes(19) := 4.U
    set(20, 2, packet.operation); set(22, 6, packet.senderMac); set(28, 4, packet.senderIp)
    set(32, 6, packet.targetMac); set(38, 4, packet.targetIp)
    val words = VecInit((0 until 4).map(i => VecInit((i * 16 until i * 16 + 16).map(bytes(_))).asUInt))
    io.in.ready := !busy
    io.out.valid := busy
    io.out.bits.data := words(index)
    io.out.bits.keep := Mux(index === 3.U, "h0fff".U, "hffff".U)
    io.out.bits.last := index === 3.U
    io.out.bits.error := false.B
    io.complete := io.out.fire && io.out.bits.last
    when(io.in.fire) { packet := io.in.bits; busy := true.B; index := 0.U }
    when(io.out.fire) {
        index := index + 1.U
        when(io.out.bits.last) { busy := false.B }
    }
}
