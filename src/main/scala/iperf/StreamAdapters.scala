package iperf

import chisel3._
import chisel3.util._
import netstack._
import soc.ip.ethernet._

/** Packet boundaries do not delimit TCP application messages. */
class PacketBytes extends Module {
    val io = IO(new Bundle {
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val out = Decoupled(UInt(8.W))
        val empty = Output(Bool())
        val clear = Input(Bool())
    })
    val data = Reg(UInt(128.W))
    val count = RegInit(0.U(5.W))
    io.in.ready := count === 0.U
    io.out.valid := count =/= 0.U
    io.out.bits := data(7, 0)
    io.empty := count === 0.U
    when(io.in.fire) { data := io.in.bits.data; count := PopCount(io.in.bits.keep) }
    when(io.out.fire) { data := data >> 8; count := count - 1.U }
    when(io.clear) { count := 0.U }
}

/** Four native MAC words become one low-byte-first protocol beat. */
class MacPack extends Module {
    val io = IO(new Bundle {
        val in = Flipped(Decoupled(new EthernetFrameBeat(4)))
        val out = Decoupled(new PacketBeat(16))
    })
    val words = Reg(Vec(4, UInt(32.W)))
    val masks = Reg(Vec(4, UInt(4.W)))
    val index = RegInit(0.U(2.W))
    val valid = RegInit(false.B)
    val last = Reg(Bool())
    val bad = RegInit(false.B)
    io.in.ready := !valid || io.out.ready
    io.out.valid := valid
    io.out.bits.data := words.asUInt
    io.out.bits.keep := masks.asUInt
    io.out.bits.last := last
    io.out.bits.error := bad
    when(io.out.fire) { valid := false.B; bad := false.B }
    when(io.in.fire) {
        for (i <- 0 until 4) {
            when(index === 0.U) { masks(i) := 0.U }
        }
        words(index) := io.in.bits.data
        masks(index) := io.in.bits.keep
        bad := Mux(index === 0.U, io.in.bits.bad, bad || io.in.bits.bad)
        when(io.in.bits.last || index === 3.U) {
            valid := true.B; last := io.in.bits.last; index := 0.U
        }.otherwise { index := index + 1.U }
    }
}

/** Registered width conversion; holds every native word under backpressure. */
class MacUnpack extends Module {
    val io = IO(new Bundle {
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val out = Decoupled(new EthernetFrameBeat(4))
    })
    val held = Reg(new PacketBeat(16))
    val valid = RegInit(false.B)
    val index = RegInit(0.U(2.W))
    val finalWord = index === 3.U || (held.keep >> ((index +& 1.U) << 2)) === 0.U
    io.in.ready := !valid || (io.out.ready && finalWord)
    io.out.valid := valid
    io.out.bits.data := (held.data >> (index << 5))(31, 0)
    io.out.bits.keep := (held.keep >> (index << 2))(3, 0)
    io.out.bits.last := held.last && finalWord
    io.out.bits.bad := held.error
    when(io.out.fire) {
        when(finalWord) { valid := false.B; index := 0.U }
            .otherwise { index := index + 1.U }
    }
    when(io.in.fire) { held := io.in.bits; valid := true.B; index := 0.U }
}

/** Binary-to-BCD, 64 clocks, no combinational divider in the result path. */
class Decimal64 extends Module {
    val io = IO(new Bundle {
        val start = Flipped(Valid(UInt(64.W)))
        val busy = Output(Bool())
        val digits = Output(Vec(20, UInt(4.W))) // least significant first
    })
    val binary = Reg(UInt(64.W))
    val digits = RegInit(0.U(80.W))
    val count = RegInit(0.U(7.W))
    io.busy := count =/= 0.U
    for(i <- 0 until 20) { io.digits(i) := digits(4*i+3, 4*i) }
    when(io.start.valid) { binary := io.start.bits; digits := 0.U; count := 64.U }
    .elsewhen(count =/= 0.U) {
        val corrected = Cat((0 until 20).reverse.map { i =>
            val d = digits(4*i+3, 4*i)
            Mux(d >= 5.U, (d + 3.U)(3,0), d)
        })
        digits := Cat(corrected(78,0), binary(63))
        binary := binary << 1
        count := count - 1.U
    }
}
