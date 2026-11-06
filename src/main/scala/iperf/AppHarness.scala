package iperf
import chisel3._
import chisel3.util._
import netstack._

class SocketTestPort extends Bundle {
    val connected = Input(Bool()); val disconnected = Input(Bool())
    val listening = Output(Bool()); val close = Output(Bool())
    val rxLo = Input(UInt(64.W)); val rxHi = Input(UInt(64.W)); val rxKeep = Input(UInt(16.W))
    val rxValid = Input(Bool()); val rxReady = Output(Bool())
    val txLo = Output(UInt(64.W)); val txHi = Output(UInt(64.W)); val txKeep = Output(UInt(16.W))
    val txValid = Output(Bool()); val txReady = Input(Bool())
}
class AppHarness extends Module {
    val io = IO(new Bundle {
        val control = new SocketTestPort; val data = new SocketTestPort
        val tick = Input(Bool())
        val phase = Output(UInt(5.W)); val errors = Output(UInt(32.W))
        val rxBytes = Output(UInt(64.W)); val txBytes = Output(UInt(64.W))
    })
    val app = Module(new Iperf3Application)
    app.io.localIp := Address.ip.U; app.io.tick := io.tick; app.io.enabled := true.B
    io.phase := app.io.phase; io.errors := app.io.errors
    io.rxBytes := app.io.rxBytes; io.txBytes := app.io.txBytes
    for(i <- 0 until 2) {
        val p = app.io.connections(i); val s = Seq(io.control, io.data)(i)
        val state = RegInit(0.U(4.W))
        p.state := state
        p.open.ready := state === 0.U
        when(p.open.fire) { state := 1.U }
        when(s.connected) { state := 4.U }
        when(s.disconnected) { state := 0.U }
        s.listening := state === 1.U
        p.close.ready := true.B; s.close := p.close.valid
        p.notice.valid := false.B; p.notice.bits := 0.U.asTypeOf(p.notice.bits)
        p.received.valid := s.rxValid
        p.received.bits.data := Cat(s.rxHi, s.rxLo)
        p.received.bits.keep := s.rxKeep; p.received.bits.last := true.B; p.received.bits.error := false.B
        s.rxReady := p.received.ready
        val writing = RegInit(false.B)
        val stored = RegInit(false.B)
        p.allocate.ready := !writing && !stored
        when(p.allocate.fire) { writing := true.B }
        p.in.ready := writing && s.txReady
        s.txValid := writing && p.in.valid
        s.txLo := p.in.bits.data(63,0); s.txHi := p.in.bits.data(127,64); s.txKeep := p.in.bits.keep
        when(p.in.fire && p.in.bits.last) { writing := false.B; stored := true.B }
        p.stored.valid := stored; p.stored.bits := 0.U.asTypeOf(p.stored.bits)
        when(p.stored.fire) { stored := false.B }
        when(s.disconnected) { writing := false.B; stored := false.B }
        p.openConflict := false.B; p.congestionLimit := 131072.U; p.congestionThreshold := 131072.U
        p.fastRecovery := false.B; p.timeout.valid := false.B; p.timeout.bits := 0.U
        p.generation := 1.U; p.sendOldest := 0.U; p.sendNext := 0.U; p.sendActual := 0.U
        p.receiveNext := 0.U; p.sendMss := 1460.U; p.receiveWindow := 262144.U; p.freeBanks := 64.U
        p.outstanding := 0.U; p.timerRunning := false.B; p.timerExpired := false.B
        p.rto := 1000.U; p.retries := 0.U; p.reorderCount := 0.U; p.reorderDropped := false.B
        p.completionAccepted := false.B
        p.pathMtu := 1500.U
        p.networkError.valid := false.B; p.networkError.bits := 0.U.asTypeOf(p.networkError.bits)
    }
}
