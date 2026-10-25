package netstack
import circt.stage.ChiselStage
import java.nio.file.{Files, Paths}
object EmitMultiProductionRx extends App {
  Files.writeString(Paths.get(args(0)), ChiselStage.emitCHIRRTL(new MultiEthernetTcpHarness))
}
