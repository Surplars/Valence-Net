package iperf

import chisel3._
import chisel3.util._
import netstack._

/** CPU-free iperf3 single-stream TCP server. Control and data are separate TCBs.
  * Implements cookie association, framed JSON, normal/reverse byte or time tests,
  * real application byte counters, result exchange, close and automatic relisten.
  */
class Iperf3Application(txBanks: Int = 8, completionDepth: Int = 16) extends Module {
    val io = IO(new Bundle {
        val connections = Flipped(Vec(2, new MultiTcpApplicationPort(txBanks, completionDepth)))
        val localIp = Input(UInt(32.W))
        val tick = Input(Bool())
        val enabled = Input(Bool())
        val phase = Output(UInt(5.W))
        val reverse = Output(Bool())
        val rxBytes = Output(UInt(64.W))
        val txBytes = Output(UInt(64.W))
        val milliseconds = Output(UInt(64.W))
        val errors = Output(UInt(32.W))
    })
    val boot :: cookie :: parameterLength :: parameters :: checkParameters :: dataOpen :: dataCookie :: running :: finishData :: resultLength :: clientResults :: convert :: convertWait :: waitDone :: closing :: waitSend :: Nil = Enum(16)
    val phase = RegInit(boot)
    val c = io.connections(0)
    val d = io.connections(1)
    val cp = Module(new PacketBytes)
    val dp = Module(new PacketBytes)
    cp.io.in <> c.received
    dp.io.in.valid := d.received.valid && phase === dataCookie
    dp.io.in.bits := d.received.bits
    cp.io.clear := phase === boot
    dp.io.clear := phase === boot
    cp.io.out.ready := false.B
    dp.io.out.ready := phase === dataCookie
    d.received.ready := Mux(phase === dataCookie, dp.io.in.ready, phase =/= boot && phase =/= dataOpen)
    val parser = Module(new IperfParameters)
    parser.io.clear := phase === boot
    parser.io.in.valid := phase === parameters && cp.io.out.fire
    parser.io.in.bits := cp.io.out.bits
    val sessionCookie = Reg(Vec(37, UInt(8.W)))
    val cookieIndex = RegInit(0.U(6.W))
    val cookieBad = RegInit(false.B)
    val lengthBytes = RegInit(0.U(3.W))
    val jsonLength = RegInit(0.U(32.W))
    val jsonRemaining = RegInit(0.U(32.W))
    val reverse = RegInit(false.B)
    val targetBytes = RegInit(0.U(64.W))
    val rxBytes = RegInit(0.U(64.W))
    val txBytes = RegInit(0.U(64.W))
    val elapsed = RegInit(0.U(64.W))
    val errors = RegInit(0.U(32.W))
    val idleTicks = RegInit(0.U(16.W))
    val isn = RegInit("h1a010000".U(32.W))
    isn := isn + 1.U
    io.phase := phase; io.reverse := reverse
    io.rxBytes := rxBytes; io.txBytes := txBytes
    io.milliseconds := elapsed; io.errors := errors
    for ((p, i) <- io.connections.zipWithIndex) {
        p.open.valid := (if(i == 0) phase === boot else phase === dataOpen) && io.enabled
        p.open.bits.passive := true.B
        p.open.bits.localIp := io.localIp; p.open.bits.remoteIp := 0.U
        p.open.bits.localPort := 5201.U; p.open.bits.remotePort := 0.U
        p.open.bits.initialSequence := (0x1a010000L + i * 1048576L).U
        p.open.bits.localMss := 1460.U; p.open.bits.offerScale := true.B
        p.open.bits.localScale := 3.U
        p.passiveInitialSequence := isn + (i * 1048576).U
        p.congestionWindow := 131072.U
        p.notice.ready := true.B
        p.close.valid := false.B
        p.close.bits := false.B
        p.allocate.valid := false.B; p.allocate.bits.length := 0.U; p.allocate.bits.context := 0.U
        p.in.valid := false.B; p.in.bits := 0.U.asTypeOf(p.in.bits)
        p.stored.ready := true.B
    }

    // A bounded generator completes every allocated write even after TEST_END.
    val gIdle :: gAllocate :: gFill :: gStored :: Nil = Enum(4)
    val generator = RegInit(gIdle)
    val writeLength = Reg(UInt(16.W))
    val writeOffset = RegInit(0.U(16.W))
    val pattern = RegInit(0.U(8.W))
    when(generator === gIdle && phase === running && reverse &&
        (targetBytes === 0.U || txBytes < targetBytes)) {
        writeLength := Mux(targetBytes =/= 0.U && targetBytes - txBytes < 1460.U,
            (targetBytes - txBytes)(15,0), 1460.U)
        generator := gAllocate
    }
    d.allocate.valid := generator === gAllocate
    d.allocate.bits.length := writeLength
    when(d.allocate.fire) { writeOffset := 0.U; generator := gFill }
    val writeRemaining = writeLength - writeOffset
    d.in.valid := generator === gFill
    d.in.bits.data := Cat((0 until 16).reverse.map(i => (pattern + i.U)(7,0)))
    d.in.bits.keep := Mux(writeRemaining >= 16.U, "hffff".U, ((1.U(17.W) << writeRemaining(3,0)) - 1.U)(15,0))
    d.in.bits.last := writeRemaining <= 16.U
    d.in.bits.error := false.B
    when(d.in.fire) {
        writeOffset := writeOffset + 16.U; pattern := pattern + 16.U
        when(d.in.bits.last) { generator := gStored }
    }
    when(d.stored.fire && generator === gStored) {
        generator := gIdle
        when(!d.stored.bits.error) { txBytes := txBytes + writeLength }
    }
    when(phase === running && d.received.fire && !reverse) {
        rxBytes := rxBytes + PopCount(d.received.bits.keep)
    }
    when(io.tick && phase === running) { elapsed := elapsed + 1.U }

    val byteDecimal = Module(new Decimal64)
    val timeDecimal = Module(new Decimal64)
    byteDecimal.io.start.valid := phase === convert
    byteDecimal.io.start.bits := Mux(reverse, txBytes, rxBytes)
    timeDecimal.io.start.valid := phase === convert
    timeDecimal.io.start.bits := Mux(elapsed === 0.U, 1.U, elapsed)
    // Whitespace before numbers is legal JSON; leading zeroes are not.
    def digitAscii(digits: Vec[UInt], n: Int, minimum: Int): UInt = {
        val visible = if(n <= minimum) true.B else digits.drop(n).map(_ =/= 0.U).reduce(_ || _)
        Mux(visible, digits(n) + 48.U, 32.U)
    }
    val prefix = """{"cpu_util_total":0,"cpu_util_user":0,"cpu_util_system":0,"sender_has_retransmits":-1,"streams":[{"id":1,"bytes":"""
    val middle = ""","retransmits":-1,"jitter":0,"errors":0,"packets":0,"start_time":0,"end_time":"""
    val suffix = "}]}"

    val resultChars: Seq[UInt] = prefix.toSeq.map(_.toInt.U(8.W)) ++
        (0 until 20).reverse.map(i => digitAscii(byteDecimal.io.digits, i, 0)) ++
        middle.toSeq.map(_.toInt.U(8.W)) ++
        (3 until 20).reverse.map(i => digitAscii(timeDecimal.io.digits, i, 3)) ++
        Seq(46.U(8.W)) ++ (0 until 3).reverse.map(i => timeDecimal.io.digits(i) + 48.U) ++
        suffix.toSeq.map(_.toInt.U(8.W))
    val resultSize = resultChars.length
    val resultMessage = VecInit(Seq(0.U(8.W), 0.U(8.W), ((resultSize >> 8) & 255).U(8.W),
        (resultSize & 255).U(8.W)) ++ resultChars ++ Seq(14.U(8.W)))
    val errorMessage = VecInit(Seq(254,0,0,0,13,0,0,0,0).map(_.U(8.W))) // SERVER_ERROR, IEUNIMP

    // Control message writer: serial assembly avoids a large 16-port ROM mux.
    val tIdle :: tAllocate :: tFill :: tOffer :: tStored :: Nil = Enum(5)
    val writer = RegInit(tIdle)
    val messageKind = Reg(UInt(3.W))
    val messageLength = Reg(UInt(16.W))
    val messageIndex = RegInit(0.U(16.W))
    val lane = RegInit(0.U(4.W))
    val packed = Reg(Vec(16, UInt(8.W)))
    val offeredKeep = Reg(UInt(16.W))
    val offeredLast = Reg(Bool())
    val returnPhase = Reg(UInt(5.W))
    def send(kind: Int, size: Int, next: UInt): Unit = {
        messageKind := kind.U; messageLength := size.U
        messageIndex := 0.U; lane := 0.U; writer := tAllocate
        returnPhase := next; phase := waitSend
    }
    val messageByte = WireDefault(0.U(8.W))
    switch(messageKind) {
        is(0.U) { messageByte := 9.U }  // PARAM_EXCHANGE
        is(1.U) { messageByte := 10.U } // CREATE_STREAMS
        is(2.U) { messageByte := Mux(messageIndex === 0.U, 1.U, 2.U) }
        is(3.U) { messageByte := 13.U } // EXCHANGE_RESULTS
        is(4.U) { messageByte := resultMessage(messageIndex(7,0)) }
        is(5.U) { messageByte := errorMessage(messageIndex(3,0)) }
    }
    c.allocate.valid := writer === tAllocate
    c.allocate.bits.length := messageLength
    when(c.allocate.fire) { writer := tFill }
    when(writer === tFill) {
        packed(lane) := messageByte
        messageIndex := messageIndex + 1.U
        when(lane === 15.U || messageIndex === messageLength - 1.U) {
            offeredKeep := ((1.U(17.W) << (lane +& 1.U)) - 1.U)(15,0)
            offeredLast := messageIndex === messageLength - 1.U
            lane := 0.U; writer := tOffer
        }.otherwise { lane := lane + 1.U }
    }
    c.in.valid := writer === tOffer
    c.in.bits.data := packed.asUInt
    c.in.bits.keep := offeredKeep; c.in.bits.last := offeredLast; c.in.bits.error := false.B
    when(c.in.fire) { writer := Mux(offeredLast, tStored, tFill) }
    when(c.stored.fire && writer === tStored) {
        writer := tIdle
        phase := returnPhase
    }

    switch(phase) {
        is(boot) {
            cookieIndex := 0.U; cookieBad := false.B; lengthBytes := 0.U; jsonLength := 0.U
            reverse := false.B; targetBytes := 0.U; rxBytes := 0.U; txBytes := 0.U; elapsed := 0.U
            when(c.open.fire) { phase := cookie }
        }
        is(cookie) {
            cp.io.out.ready := true.B
            when(cp.io.out.fire) {
                sessionCookie(cookieIndex) := cp.io.out.bits
                cookieIndex := cookieIndex + 1.U
                when(cookieIndex === 36.U) { send(0, 1, parameterLength); cookieIndex := 0.U }
            }
        }
        is(parameterLength, resultLength) {
            cp.io.out.ready := true.B
            when(cp.io.out.fire) {
                val nextLength = Cat(jsonLength(23,0), cp.io.out.bits)
                jsonLength := nextLength; lengthBytes := lengthBytes + 1.U
                when(lengthBytes === 3.U) {
                    lengthBytes := 0.U; jsonLength := 0.U; jsonRemaining := nextLength
                    when(nextLength === 0.U || nextLength > 4096.U) {
                        errors := errors + 1.U; send(5, 9, closing)
                    }.otherwise { phase := Mux(phase === parameterLength, parameters, clientResults) }
                }
            }
        }
        is(parameters) {
            cp.io.out.ready := true.B
            when(cp.io.out.fire) {
                jsonRemaining := jsonRemaining - 1.U
                when(jsonRemaining === 1.U) { phase := checkParameters }
            }
        }
        is(checkParameters) {
            when(parser.io.accepted) {
                reverse := parser.io.reverse; targetBytes := parser.io.bytes; phase := dataOpen
            }.otherwise { errors := errors + 1.U; send(5, 9, closing) }
        }
        is(dataOpen) { when(d.open.fire) { send(1, 1, dataCookie) } }
        is(dataCookie) {
            when(dp.io.out.fire) {
                val mismatch = dp.io.out.bits =/= sessionCookie(cookieIndex)
                cookieBad := cookieBad || mismatch
                cookieIndex := cookieIndex + 1.U
                when(cookieIndex === 36.U) {
                    when(cookieBad || mismatch) { errors := errors + 1.U; send(5, 9, closing) }
                    .otherwise { send(2, 2, running) }
                }
            }
        }
        is(running) {
            cp.io.out.ready := true.B
            when(cp.io.out.fire) {
                when(cp.io.out.bits === 4.U) { phase := finishData }
            }
        }
        is(finishData) { when(generator === gIdle) { send(3, 1, resultLength) } }
        is(clientResults) {
            // Client statistics are length-delimited and discarded; our own
            // committed counters are authoritative for the returned result.
            cp.io.out.ready := true.B
            when(cp.io.out.fire) {
                jsonRemaining := jsonRemaining - 1.U
                when(jsonRemaining === 1.U) { phase := convert }
            }
        }
        is(convert) { phase := convertWait }
        is(convertWait) {
            when(!byteDecimal.io.busy && !timeDecimal.io.busy) {
                send(4, resultSize + 5, waitDone)
            }
        }
        is(waitDone) {
            cp.io.out.ready := true.B
            when(cp.io.out.fire) {
                when(cp.io.out.bits =/= 16.U) { errors := errors + 1.U }
                phase := closing
            }
        }
        is(closing) {
            cp.io.out.ready := true.B
            for(p <- io.connections) {
                p.close.valid := p.state =/= TcpState.closed.U
                p.close.bits := true.B
            }
            when(io.connections.map(p => p.state === TcpState.closed.U && p.outstanding === 0.U).reduce(_ && _)) {
                phase := boot; writer := tIdle; generator := gIdle
            }
        }
    }
    // Gracefully retire data at TEST_END; preserve allocated TX ownership.
    when(phase === resultLength || phase === clientResults || phase === convert ||
        phase === convertWait || phase === waitDone || (phase === waitSend && messageKind >= 3.U)) {
        d.close.valid := d.state === TcpState.established.U || d.state === TcpState.closeWait.U
    }
    when(io.tick && phase =/= boot) { idleTicks := idleTicks + 1.U }
    when(cp.io.out.fire || dp.io.out.fire || d.in.fire || d.received.fire || phase === boot) { idleTicks := 0.U }
    // After DISPLAY_RESULTS the peer may close a reverse data socket with
    // unread trailing bytes, producing RST. Results are already committed;
    // continue waiting for IPERF_DONE on the control connection.
    val controlFailure = c.notice.fire && (c.notice.bits.flags(3) || c.notice.bits.flags(4))
    val dataFailure = d.notice.fire && (d.notice.bits.flags(3) || d.notice.bits.flags(4)) && phase =/= waitDone
    val abortSession = controlFailure || dataFailure ||
        (d.stored.fire && generator === gStored && d.stored.bits.error) ||
        (c.stored.fire && writer === tStored && c.stored.bits.error) ||
        (phase === running && cp.io.out.fire && cp.io.out.bits =/= 4.U) ||
        (idleTicks === 30000.U && phase =/= boot) ||
        (!io.enabled && phase =/= boot) ||
        (c.notice.fire && c.notice.bits.flags(1) && phase =/= closing && phase =/= waitDone)
    when(abortSession && phase =/= closing && phase =/= boot) { phase := closing; errors := errors + 1.U }
}
