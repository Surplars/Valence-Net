package netstack

import chisel3._
import chisel3.util._

class ArpResult extends Bundle {
    val mac = UInt(48.W)
    val error = Bool()
}

/** One outstanding next-hop lookup, associative neighbor cache and ARP replies.
  * tick is a 1-ms enable in production. Retry/expiry values are elaboration
  * parameters. txComplete must pulse only when the accepted ARP frame's final
  * beat actually transfers. Flush cancels lookup/queued work but lets an already
  * started frame finish. Change local addressing together with flush.
  */
class ArpEngine(val entries: Int = 16, val retryTicks: Int = 1000,
    val lifetimeTicks: Int = 60000, val maxAttempts: Int = 3) extends Module {
    require(entries >= 2 && isPow2(entries) && retryTicks > 0 && lifetimeTicks > 0 && maxAttempts > 0)
    val io = IO(new Bundle {
        val localIp = Input(UInt(32.W))
        val localMac = Input(UInt(48.W))
        val tick = Input(Bool())
        val flush = Input(Bool())
        val rx = Flipped(Decoupled(new ArpPacket))
        val tx = Decoupled(new ArpPacket)
        val txComplete = Input(Bool())
        val resolve = Flipped(Decoupled(UInt(32.W)))
        val result = Decoupled(new ArpResult)
        val conflict = Output(Bool())
    })
    val valid = RegInit(VecInit(Seq.fill(entries)(false.B)))
    val ips = Reg(Vec(entries, UInt(32.W)))
    val macs = Reg(Vec(entries, UInt(48.W)))
    val ages = Reg(Vec(entries, UInt(math.max(1, log2Ceil(lifetimeTicks + 1)).W)))
    val victim = RegInit(0.U(log2Ceil(entries).W))
    val busy = RegInit(false.B)
    val target = Reg(UInt(32.W))
    val sourceIp = Reg(UInt(32.W))
    val sourceMac = Reg(UInt(48.W))
    val generation = RegInit(0.U(32.W))
    val probePending = RegInit(false.B)
    val waiting = RegInit(false.B)
    val timer = RegInit(0.U(math.max(1, log2Ceil(retryTicks + 1)).W))
    val attempts = RegInit(0.U(math.max(1, log2Ceil(maxAttempts + 1)).W))
    val resultValid = RegInit(false.B)
    val result = Reg(new ArpResult)
    val txPending = RegInit(false.B)
    val txPacket = Reg(new ArpPacket)
    val txProbe = RegInit(false.B)
    val txGeneration = Reg(UInt(32.W))
    val transmitting = RegInit(false.B)
    val flightProbe = Reg(Bool())
    val flightGeneration = Reg(UInt(32.W))

    io.result.valid := resultValid && !io.flush
    io.result.bits := result
    io.resolve.ready := !busy && !resultValid && !io.flush
    io.tx.valid := txPending && !transmitting && !io.flush
    io.tx.bits := txPacket
    val slot = !txPending || io.tx.fire
    // A pending resolution gets one slot before accepting more reply work.
    val makeProbe = probePending && slot && !io.flush
    val senderValid = io.rx.bits.senderMac =/= 0.U && !io.rx.bits.senderMac(40) &&
        io.rx.bits.senderMac =/= io.localMac
    val opValid = io.rx.bits.operation === 1.U || io.rx.bits.operation === 2.U
    val localTarget = io.rx.bits.targetIp === io.localIp
    val wantsReply = senderValid && io.rx.bits.operation === 1.U && localTarget
    io.rx.ready := !io.flush && (!wantsReply || (slot && !makeProbe))
    val existing = VecInit((0 until entries).map(i => valid(i) && ips(i) === io.rx.bits.senderIp))
    val senderIpValid = io.rx.bits.senderIp =/= 0.U && io.rx.bits.senderIp =/= io.localIp &&
        io.rx.bits.senderIp =/= "hffffffff".U && io.rx.bits.senderIp(31, 28) =/= 14.U
    val learn = io.rx.fire && senderValid && senderIpValid && opValid &&
        (existing.asUInt.orR || (localTarget && (io.rx.bits.operation === 1.U || io.rx.bits.targetMac === io.localMac)))
    io.conflict := io.rx.fire && senderValid && opValid && io.rx.bits.senderIp === io.localIp
    val effectiveValid = VecInit((0 until entries).map(i => valid(i) && !(io.tick && ages(i) === (lifetimeTicks - 1).U)))
    val hits = VecInit((0 until entries).map(i => effectiveValid(i) && ips(i) === io.resolve.bits))
    val bypass = learn && io.rx.bits.senderIp === io.resolve.bits
    val hitMac = Mux(bypass, io.rx.bits.senderMac, macs(PriorityEncoder(hits.asUInt)))

    when(io.result.fire) { resultValid := false.B }
    when(io.tx.fire) {
        txPending := false.B
        transmitting := true.B
        flightProbe := txProbe
        flightGeneration := txGeneration
    }
    when(io.txComplete && transmitting) {
        transmitting := false.B
        when(flightProbe && busy && flightGeneration === generation) {
            waiting := true.B
            timer := 0.U
            attempts := attempts + 1.U
        }
    }
    when(makeProbe) {
        txPending := true.B
        txProbe := true.B
        txGeneration := generation
        txPacket.operation := 1.U
        txPacket.senderMac := sourceMac
        txPacket.senderIp := sourceIp
        txPacket.targetMac := 0.U
        txPacket.targetIp := target
        probePending := false.B
    }
    when(io.rx.fire && wantsReply) {
        txPending := true.B
        txProbe := false.B
        txGeneration := generation
        txPacket.operation := 2.U
        txPacket.senderMac := io.localMac
        txPacket.senderIp := io.localIp
        txPacket.targetMac := io.rx.bits.senderMac
        txPacket.targetIp := io.rx.bits.senderIp
    }
    when(io.tick) {
        for (i <- 0 until entries) {
            when(valid(i)) {
                when(ages(i) === (lifetimeTicks - 1).U) { valid(i) := false.B }
                    .otherwise { ages(i) := ages(i) + 1.U }
            }
        }
        when(busy && waiting) {
            when(timer === (retryTicks - 1).U) {
                waiting := false.B
                timer := 0.U
                when(attempts >= maxAttempts.U) {
                    busy := false.B
                    resultValid := true.B
                    result.mac := 0.U
                    result.error := true.B
                }.otherwise { probePending := true.B }
            }.otherwise { timer := timer + 1.U }
        }
    }
    when(learn) {
        val free = ~effectiveValid.asUInt
        val index = Mux(existing.asUInt.orR, PriorityEncoder(existing.asUInt),
            Mux(free.orR, PriorityEncoder(free), victim))
        valid(index) := true.B
        ips(index) := io.rx.bits.senderIp
        macs(index) := io.rx.bits.senderMac
        ages(index) := 0.U
        when(!existing.asUInt.orR) { victim := victim + 1.U }
        when(busy && io.rx.bits.senderIp === target) {
            busy := false.B
            waiting := false.B
            probePending := false.B
            resultValid := true.B
            result.mac := io.rx.bits.senderMac
            result.error := false.B
            when(!wantsReply && (makeProbe || (txPending && txProbe && txGeneration === generation && !io.tx.fire))) {
                txPending := false.B
            }
        }
    }
    when(io.resolve.fire) {
        generation := generation + 1.U
        val localHit = io.resolve.bits === io.localIp
        val broadcastHit = io.resolve.bits === "hffffffff".U
        when(localHit || broadcastHit || hits.asUInt.orR || bypass || io.resolve.bits === 0.U) {
            resultValid := true.B
            result.error := io.resolve.bits === 0.U
            result.mac := Mux(localHit, io.localMac,
                Mux(broadcastHit, "hffffffffffff".U, Mux(io.resolve.bits === 0.U, 0.U, hitMac)))
        }.otherwise {
            busy := true.B
            target := io.resolve.bits
            sourceIp := io.localIp
            sourceMac := io.localMac
            probePending := true.B
            waiting := false.B
            attempts := 0.U
            timer := 0.U
        }
    }
    when(io.flush) {
        valid.foreach(_ := false.B)
        victim := 0.U
        busy := false.B
        waiting := false.B
        probePending := false.B
        txPending := false.B
        generation := generation + 1.U
        when(busy || resultValid) {
            resultValid := true.B
            result.mac := 0.U
            result.error := true.B
        }
    }
}

class ArpHarness extends Module {
    val io = IO(new Bundle {
        val lo = Input(UInt(64.W)); val hi = Input(UInt(64.W))
        val keep = Input(UInt(16.W)); val last = Input(Bool()); val error = Input(Bool())
        val valid = Input(Bool()); val ready = Output(Bool())
        val outReady = Input(Bool()); val outValid = Output(Bool())
        val outLo = Output(UInt(64.W)); val outHi = Output(UInt(64.W))
        val outKeep = Output(UInt(16.W)); val outLast = Output(Bool())
        val queryValid = Input(Bool()); val queryReady = Output(Bool()); val queryIp = Input(UInt(32.W))
        val resultReady = Input(Bool()); val resultValid = Output(Bool())
        val mac = Output(UInt(48.W)); val failed = Output(Bool())
        val tick = Input(Bool()); val flush = Input(Bool())
        val badFrame = Output(Bool()); val conflict = Output(Bool())
    })
    val dut = Module(new ArpEndpoint(4, 8, 40))
    dut.io.localIp := "hc0000202".U
    dut.io.localMac := "h020000000002".U
    dut.io.tick := io.tick; dut.io.flush := io.flush
    dut.io.in.valid := io.valid; io.ready := dut.io.in.ready
    dut.io.in.bits.data := Cat(io.hi, io.lo); dut.io.in.bits.keep := io.keep
    dut.io.in.bits.last := io.last; dut.io.in.bits.error := io.error
    dut.io.out.ready := io.outReady; io.outValid := dut.io.out.valid
    io.outLo := dut.io.out.bits.data(63, 0); io.outHi := dut.io.out.bits.data(127, 64)
    io.outKeep := dut.io.out.bits.keep; io.outLast := dut.io.out.bits.last
    dut.io.resolve.valid := io.queryValid; dut.io.resolve.bits := io.queryIp; io.queryReady := dut.io.resolve.ready
    dut.io.result.ready := io.resultReady; io.resultValid := dut.io.result.valid
    io.mac := dut.io.result.bits.mac; io.failed := dut.io.result.bits.error
    io.badFrame := dut.io.badFrame; io.conflict := dut.io.conflict
}

class ArpEndpoint(val entries: Int = 16, val retryTicks: Int = 1000, val lifetimeTicks: Int = 60000) extends Module {
    val io = IO(new Bundle {
        val localIp = Input(UInt(32.W))
        val localMac = Input(UInt(48.W))
        val tick = Input(Bool())
        val flush = Input(Bool())
        val in = Flipped(Decoupled(new PacketBeat(16)))
        val out = Decoupled(new PacketBeat(16))
        val resolve = Flipped(Decoupled(UInt(32.W)))
        val result = Decoupled(new ArpResult)
        val badFrame = Output(Bool())
        val conflict = Output(Bool())
    })
    val rx = Module(new ArpFrameReceive)
    val tx = Module(new ArpFrameTransmit)
    val engine = Module(new ArpEngine(entries, retryTicks, lifetimeTicks))
    rx.io.in <> io.in
    rx.io.localMac := io.localMac
    engine.io.localIp := io.localIp
    engine.io.localMac := io.localMac
    engine.io.tick := io.tick
    engine.io.flush := io.flush
    engine.io.rx <> rx.io.out
    tx.io.in <> engine.io.tx
    engine.io.txComplete := tx.io.complete
    io.out <> tx.io.out
    engine.io.resolve <> io.resolve
    io.result <> engine.io.result
    io.badFrame := rx.io.dropped
    io.conflict := engine.io.conflict
}
