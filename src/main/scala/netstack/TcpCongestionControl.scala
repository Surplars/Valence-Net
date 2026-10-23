package netstack

import chisel3._
import chisel3.util._

/** Conservative RFC 5681 Reno: one-SMSS initial/restart window, byte-counted
  * slow start and congestion avoidance, three-duplicate-ACK fast retransmit,
  * fast recovery and RTO loss response. One-SMSS IW also covers a lost SYN.
  * Input ACKs have already passed the TCB's sequence/window checks.
  * No SACK/NewReno/ECN or limited transmit is claimed by this controller.
  */
class TcpCongestionControl(val maxWindow: Int = 16777215) extends Module {
    require(maxWindow >= 131070 && maxWindow < 1073741824)
    val io = IO(new Bundle {
        val clear = Input(Bool()); val mss = Input(UInt(16.W))
        val ack = Flipped(Valid(new TcpAckUpdate))
        val oldest = Input(UInt(32.W)); val flight = Input(UInt(32.W))
        val timeout = Input(Bool()); val idleRestart = Input(Bool())
        val window = Output(UInt(32.W)); val threshold = Output(UInt(32.W))
        val recovering = Output(Bool()); val duplicates = Output(UInt(3.W))
        val replay = Decoupled(UInt(32.W))
    })
    def min(a: UInt, b: UInt): UInt = Mux(a < b, a, b)
    def max(a: UInt, b: UInt): UInt = Mux(a > b, a, b)
    def cap(a: UInt): UInt = min(a, maxWindow.U)
    val mss = Mux(io.mss === 0.U, 1.U, io.mss).pad(32)
    val cwnd = RegInit(1.U(32.W))
    val threshold = RegInit(maxWindow.U(32.W))
    val acknowledged = RegInit(0.U(32.W))
    val duplicates = RegInit(0.U(3.W))
    val recovering = RegInit(false.B)
    val lossOutstanding = RegInit(false.B)
    val replayPending = RegInit(false.B)
    val replaySequence = Reg(UInt(32.W))
    val timeoutSeen = RegNext(io.timeout, false.B)
    val newTimeout = io.timeout && !timeoutSeen
    val amount = io.ack.bits.acknowledgement - io.ack.bits.previous
    val progress = io.ack.valid && amount =/= 0.U && !amount(31)
    val reduced = cap(max(io.flight >> 1, mss << 1))
    io.replay.valid := replayPending; io.replay.bits := replaySequence
    when(io.replay.fire) { replayPending := false.B }
    when(io.ack.valid) {
        when(io.ack.bits.handshake) {
            cwnd := mss; acknowledged := 0.U; duplicates := 0.U
        }.elsewhen(progress) {
            duplicates := 0.U; replayPending := false.B; lossOutstanding := false.B
            when(recovering) {
                cwnd := threshold; recovering := false.B; acknowledged := 0.U
            }.elsewhen(cwnd < threshold) {
                cwnd := cap(cwnd +& min(amount, mss))
            }.otherwise {
                val credit = acknowledged +& amount
                when(credit >= cwnd) { cwnd := cap(cwnd +& mss); acknowledged := 0.U }
                    .otherwise { acknowledged := credit }
            }
        }.elsewhen(io.ack.bits.duplicate) {
            when(recovering) { cwnd := cap(cwnd +& mss) }
                .elsewhen(duplicates === 2.U) {
                    threshold := reduced; cwnd := cap(reduced +& (mss * 3.U))
                    recovering := true.B; acknowledged := 0.U; duplicates := 3.U
                    replayPending := true.B; replaySequence := io.oldest
                }.otherwise { duplicates := duplicates + 1.U }
        }
    }
    when(io.idleRestart && io.flight === 0.U) {
        cwnd := min(cwnd, mss); acknowledged := 0.U; duplicates := 0.U; recovering := false.B
    }
    when(newTimeout && !progress) {
        when(!lossOutstanding) { threshold := reduced }
        cwnd := mss; acknowledged := 0.U; recovering := false.B
        duplicates := 0.U; replayPending := false.B; lossOutstanding := true.B
    }
    when(io.clear) {
        cwnd := mss; threshold := maxWindow.U; acknowledged := 0.U; duplicates := 0.U
        recovering := false.B; lossOutstanding := false.B; replayPending := false.B
    }
    io.window := cwnd; io.threshold := threshold; io.recovering := recovering; io.duplicates := duplicates
}

class TcpCongestionHarness extends Module {
    val io = IO(new Bundle {
        val clear = Input(Bool()); val mss = Input(UInt(16.W))
        val ackValid = Input(Bool()); val previous = Input(UInt(32.W)); val acknowledgement = Input(UInt(32.W))
        val handshake = Input(Bool()); val duplicate = Input(Bool())
        val oldest = Input(UInt(32.W)); val flight = Input(UInt(32.W))
        val timeout = Input(Bool()); val idleRestart = Input(Bool()); val replayReady = Input(Bool())
        val window = Output(UInt(32.W)); val threshold = Output(UInt(32.W)); val recovering = Output(Bool())
        val duplicates = Output(UInt(3.W)); val replayValid = Output(Bool()); val replaySequence = Output(UInt(32.W))
    })
    val dut = Module(new TcpCongestionControl)
    dut.io.clear := io.clear; dut.io.mss := io.mss
    dut.io.ack.valid := io.ackValid; dut.io.ack.bits := 0.U.asTypeOf(new TcpAckUpdate)
    dut.io.ack.bits.previous := io.previous; dut.io.ack.bits.acknowledgement := io.acknowledgement
    dut.io.ack.bits.handshake := io.handshake; dut.io.ack.bits.duplicate := io.duplicate
    dut.io.oldest := io.oldest; dut.io.flight := io.flight
    dut.io.timeout := io.timeout; dut.io.idleRestart := io.idleRestart; dut.io.replay.ready := io.replayReady
    io.window := dut.io.window; io.threshold := dut.io.threshold; io.recovering := dut.io.recovering
    io.duplicates := dut.io.duplicates; io.replayValid := dut.io.replay.valid; io.replaySequence := dut.io.replay.bits
}
