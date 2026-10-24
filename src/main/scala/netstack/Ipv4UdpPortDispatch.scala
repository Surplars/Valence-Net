package netstack

import chisel3._
import chisel3.util._

/** Optional UDP binding table. Empty bindings mean the existing wildcard
  * application stream and this module is not instantiated. Closed ports send
  * the whole original packet to the independently validating ICMP generator.
  * Only the optional UDP path pays the two-header-beat classification delay.
  */
class Ipv4UdpPortDispatch(val ports: Seq[Int]) extends Module {
    require(ports.nonEmpty && ports.distinct.size == ports.size && ports.forall(p => p > 0 && p < 65536))
    val io = IO(new Bundle {
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val suppressErrors = Input(Bool())
        val accepted = Decoupled(new PacketBeat(16))
        val rejected = Decoupled(new PacketBeat(16))
    })
    val first :: second :: decide :: prefix :: stream :: Nil = Enum(5)
    val state = RegInit(first)
    val header = Reg(Vec(2, new PacketBeat(16)))
    val headerCount = Reg(UInt(2.W)); val index = RegInit(false.B)
    val open = RegInit(false.B); val suppress = RegInit(false.B)
    io.in.ready := state === first || state === second
    io.accepted.valid := false.B; io.accepted.bits := io.in.bits
    io.rejected.valid := false.B; io.rejected.bits := io.in.bits
    when(state === first && io.in.fire) {
        header(0) := io.in.bits; suppress := io.suppressErrors
        headerCount := 1.U; state := Mux(io.in.bits.last, decide, second)
    }
    when(state === second && io.in.fire) {
        header(1) := io.in.bits; headerCount := 2.U; state := decide
    }
    val destination = Cat(header(1).data(55, 48), header(1).data(63, 56))
    when(state === decide) {
        open := headerCount === 2.U && ports.map(p => destination === p.U).reduce(_ || _)
        index := false.B; state := prefix
    }
    val discard = !open && suppress
    val ready = Mux(open, io.accepted.ready, discard || io.rejected.ready)
    when(state === prefix || state === stream) {
        val bits = Mux(state === prefix, header(index), io.in.bits)
        val valid = state === prefix || io.in.valid
        io.accepted.valid := valid && open; io.accepted.bits := bits
        io.rejected.valid := valid && !open && !discard; io.rejected.bits := bits
        when(state === stream) { io.in.ready := ready }
        when(valid && ready) {
            when(bits.last) { state := first }
                .elsewhen(state === prefix) {
                    when(index || headerCount === 1.U) { state := stream }
                        .otherwise { index := true.B }
                }
        }
    }
}
