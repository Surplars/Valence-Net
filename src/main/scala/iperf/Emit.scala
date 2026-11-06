package iperf
import chisel3._
import circt.stage.ChiselStage
import java.nio.file.{Files, Paths}
object Emit extends App {
    require(args.length == 2, "usage: iperf.Emit app|gmii|board OUTPUT")
    val out = Paths.get(args(1)); Files.createDirectories(out)
    def generate: RawModule = args(0) match {
        case "app" => new AppHarness
        case "gmii" => new IperfGmiiHarness
        case "board" => new IperfBoardCore
        case "params" => new IperfParameters
        case "rxqueue" => new ReceiveWindowHarness
        case "rxdiag" => new RxDiagnosticSnapshot(sampleCycles = 32)
        case _ => throw new IllegalArgumentException(args(0))
    }
    Files.writeString(out.resolve(args(0) + ".fir"), ChiselStage.emitCHIRRTL(generate))
    ChiselStage.emitSystemVerilogFile(generate, Array("--target-dir", out.resolve("rtl").toString),
        Array("-disable-all-randomization", "-strip-debug-info"))
}
