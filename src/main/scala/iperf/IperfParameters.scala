package iperf

import chisel3._
import chisel3.util._

/** Bounded JSON object parser for iperf3's scalar parameter dictionary.
  * Ignores unknown scalar metadata, rejects compound values, escapes in keys,
  * invalid numbers and unsupported traffic modes. TCP chunking is irrelevant.
  */
class IperfParameters extends Module {
    val io = IO(new Bundle {
        val clear = Input(Bool())
        val in = Flipped(Valid(UInt(8.W)))
        val complete = Output(Bool())
        val accepted = Output(Bool())
        val reverse = Output(Bool())
        val bytes = Output(UInt(64.W))
    })
    val begin :: keyOrEnd :: key :: colon :: value :: scalar :: string :: delimiter :: done :: Nil = Enum(9)
    val state = RegInit(begin)
    val bad = RegInit(false.B)
    val tcp = RegInit(false.B)
    val reverse = RegInit(false.B)
    val bytes = RegInit(0.U(64.W))
    val keyValue = RegInit(0.U(192.W))
    val keyLength = RegInit(0.U(6.W))
    val token = RegInit(0.U(40.W))
    val tokenLength = RegInit(0.U(6.W))
    val number = RegInit(0.U(64.W))
    val numeric = RegInit(true.B)
    val leadingZero = RegInit(false.B)
    val escaped = RegInit(false.B)
    val afterComma = RegInit(false.B)
    val seen = RegInit(0.U(16.W))
    val c = io.in.bits
    val white = c === 32.U || c === 9.U || c === 10.U || c === 13.U
    def keyIs(s: String): Bool = keyLength === s.length.U && keyValue === BigInt(1, s.getBytes("UTF-8")).U
    def wordIs(s: String): Bool = tokenLength === s.length.U && token === BigInt(1, s.getBytes("UTF-8")).U
    val isTrue = wordIs("true")
    val isFalse = wordIs("false")
    val boolValid = isTrue || isFalse || (numeric && tokenLength === 1.U && number <= 1.U)
    val boolValue = isTrue || (numeric && number === 1.U)
    def applyValue(quoted: Boolean): Unit = {
        val keys = Seq("tcp", "udp", "sctp", "parallel", "reverse", "bidirectional", "omit",
            "num", "blockcount", "bandwidth", "burst", "authtoken", "mptcp")
        for ((s, i) <- keys.zipWithIndex) {
            when(keyIs(s)) {
                when(seen(i)) { bad := true.B }
                seen := seen | (BigInt(1) << i).U
                if(quoted) { bad := true.B }
            }
        }
        if (!quoted) {
            when(!numeric && !isTrue && !isFalse && !wordIs("null")) { bad := true.B }
            when(keyIs("tcp")) { tcp := boolValue; when(!boolValid || !boolValue) { bad := true.B } }
            when(keyIs("reverse")) { reverse := boolValue; when(!boolValid) { bad := true.B } }
            when(keyIs("parallel")) { when(!numeric || number =/= 1.U) { bad := true.B } }
            when(keyIs("num")) { bytes := number; when(!numeric) { bad := true.B } }
            for(s <- Seq("udp", "sctp", "bidirectional", "mptcp")) {
                when(keyIs(s) && (!boolValid || boolValue)) { bad := true.B }
            }
            for(s <- Seq("omit", "blockcount", "bandwidth", "burst")) {
                when(keyIs(s) && (!numeric || number =/= 0.U)) { bad := true.B }
            }
            when(keyIs("authtoken")) { bad := true.B }
        }
    }
    io.complete := state === done
    io.accepted := state === done && !bad && tcp
    io.reverse := reverse
    io.bytes := bytes
    when(io.clear) {
        state := begin; bad := false.B; tcp := false.B; reverse := false.B; bytes := 0.U
        seen := 0.U; afterComma := false.B
    }.elsewhen(io.in.valid) {
        switch(state) {
            is(begin) { when(!white) { when(c === 123.U) { state := keyOrEnd }.otherwise { bad := true.B } } }
            is(keyOrEnd) {
                when(!white) {
                    when(c === 34.U) { state := key; keyValue := 0.U; keyLength := 0.U }
                    .elsewhen(c === 125.U && !afterComma) { state := done }
                    .otherwise { bad := true.B }
                }
            }
            is(key) {
                when(c === 34.U) { state := colon }
                .otherwise {
                    when(c < 32.U || c === 92.U || keyLength === 24.U) { bad := true.B }
                    when(keyLength < 24.U) { keyValue := Cat(keyValue(183,0), c); keyLength := keyLength + 1.U }
                }
            }
            is(colon) {
                when(!white) { when(c === 58.U) { state := value }.otherwise { bad := true.B } }
            }
            is(value) {
                when(!white) {
                    token := c; tokenLength := 1.U
                    numeric := c >= 48.U && c <= 57.U
                    number := Mux(c >= 48.U && c <= 57.U, c - 48.U, 0.U)
                    leadingZero := c === 48.U
                    when(c === 34.U) { state := string; escaped := false.B }
                    .otherwise { state := scalar }
                }
            }
            is(scalar) {
                when(white || c === 44.U || c === 125.U) {
                    applyValue(false)
                    when(c === 125.U) { state := done }
                    .elsewhen(c === 44.U) { state := keyOrEnd; afterComma := true.B }
                    .otherwise { state := delimiter }
                }.otherwise {
                    token := Cat(token(31,0), c)
                    when(tokenLength < 63.U) { tokenLength := tokenLength + 1.U }
                    .otherwise { bad := true.B }
                    numeric := numeric && c >= 48.U && c <= 57.U
                    when(numeric) {
                        val next = (number * 10.U) +& (c - 48.U)
                        when(c < 48.U || c > 57.U || leadingZero || next > "hffffffffffffffff".U) { bad := true.B }
                        number := next(63,0)
                    }
                }
            }
            is(string) {
                when(escaped) { escaped := false.B }
                .elsewhen(c === 92.U) { escaped := true.B }
                .elsewhen(c === 34.U) { applyValue(true); state := delimiter }
                when(c < 32.U) { bad := true.B }
            }
            is(delimiter) {
                when(!white) {
                    when(c === 44.U) { state := keyOrEnd; afterComma := true.B }
                    .elsewhen(c === 125.U) { state := done }
                    .otherwise { bad := true.B }
                }
            }
            is(done) { when(!white) { bad := true.B } }
        }
    }
}
