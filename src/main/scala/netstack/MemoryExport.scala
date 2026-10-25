package netstack

import chisel3.RawModule
import _root_.circt.stage.ChiselStage
import java.nio.file.{Files, Paths}
import scala.jdk.CollectionConverters._

/** Synthesis preferences on explicitly named synchronous arrays.
  * This export-only pass adds attributes, never ports, latency or behavior.
  * FIRRTL and SystemVerilog simulations use the same collision-safe RAM logic.
  * Only the current emission directory is visited; frozen runs are untouched.
  */
object MemoryExport {
    val profiles = Set("hybrid", "bram", "auto")
    var profile: String = "hybrid"
    def emit(gen: => RawModule, arguments: Array[String]): Unit = {
        require(profiles(profile))
        ChiselStage.emitSystemVerilogFile(gen, arguments)
        val index = arguments.indexOf("--target-dir")
        require(index >= 0 && index + 1 < arguments.length)
        val directory = Paths.get(arguments(index + 1))
        val paths = Files.list(directory)
        try paths.iterator.asScala.filter(_.toString.endsWith(".sv")).foreach { path =>
            val original = Files.readString(path)
            var selected: Option[String] = None
            val mapped = original.linesIterator.map { line =>
                if (line.startsWith("module ")) {
                    selected = if (profile == "auto") None
                        else if (line.startsWith("module bram_fifo_")) Some("block")
                        else if (line.startsWith("module retainedPayload_")) {
                            val dimensions = "module retainedPayload_([0-9]+)x([0-9]+).*".r
                            line match {
                                case dimensions(depth, width) if profile == "hybrid" &&
                                    depth.toInt >= 512 && width.toInt == 128 => Some("ultra")
                                case _ => Some("block")
                            }
                        } else None
                }
                if (selected.nonEmpty && !line.contains("ram_style") &&
                    line.matches(".*reg +\\[[0-9]+:0\\] +Memory\\[0:[0-9]+\\];.*"))
                    line.replaceFirst("reg +", "(* ram_style = \"" + selected.get + "\" *) reg ")
                else line
            }.mkString("\n") + "\n"
            if (mapped != original) Files.writeString(path, mapped)
        } finally paths.close()
        Files.writeString(directory.resolve("memory-profile.txt"), profile + "\n")
    }
}
