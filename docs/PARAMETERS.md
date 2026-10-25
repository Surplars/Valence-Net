# Endpoint configuration

The hardware is partly parameterized at Chisel elaboration time. Parameters
change emitted RTL and resource geometry; they are not software registers.
The production timing fixture uses the defaults of
`MultiEthernetTcpUdpEndpoint`. Different configurations require their own
functional/performance checks and physical timing closure.

| Top-level parameter | Default | Meaning and constraints |
| --- | ---: | --- |
| connectionCount | 4 | Independent TCP contexts; 1 through 8, including non-powers of two |
| packetBeats | 128 | Packet-buffer capacity in 16-byte beats; power of two, at least 2 |
| txBanks | 8 | Retained TX payload banks per context; power of two, 2 through 256 |
| txBeats | 128 | 16-byte beats per TX bank; power of two, 2 through 2048 |
| rxWords | 4096 | 16-byte words per context's receive ring; power of two, 4 through 4096 |
| arpEntries | 16 | ARP table entries; power of two, at least 2 |
| ticksPerSecond | 1000 | External tick frequency, 1 through 1,000,000; supply a matching tick input |
| timeWaitTicks | 120000 | TIME-WAIT duration in external ticks; at least 2 |
| completionDepth | 16 | Per-context ordered physical completion capacity; at least 2 |
| autonomousCongestion | true | Enable built-in congestion control; the application's congestionWindow remains an upper cap |
| udpPorts | empty | Static UDP destination-port allowlist; empty accepts all ports |

A default TCP context has 16 KiB of retained TX payload storage
(`8 * 128 * 16`) and a 64 KiB receive ring (`4096 * 16`). Four contexts
therefore have 64 KiB TX and 256 KiB RX capacity, excluding reorder retention,
packet staging, descriptors and memory packing overhead. RAM primitive use
cannot be inferred from these byte totals alone.

Local IP/MAC, netmask, gateway, MTU and session OPEN configuration are hardware
inputs. They do not change the number of instantiated contexts or the memory
geometry. Observe each module's ownership rules; local network configuration
is static while active. There is no CSR/software configuration adapter yet.

## Fixed or lower-level configuration

- The integrated stream is fixed at 128 data bits with a 16-bit keep mask.
  A change of top-level packet capacity does not change this width.
- Each receive queue currently instantiates the default eight-bank reorder
  buffer with 128 beats per bank. Its geometry is not exposed at the Ethernet
  top, and its count interface is four bits.
- Four outgoing SACK blocks and the sixteen-range sender scoreboard are fixed.
  The cold snapshot builder accepts a power-of-two bank count from 2 to 256;
  this does not widen the integrated reorder-count interface automatically.
- SACK support, challenge spacing and maximum retries are configurable in
  lower-level TCP constructors; the multi-Ethernet top does not expose every
  option. SACK remains enabled in the production default.
- UDP PMTU cache/history have lower-level parameters, defaulting to four/eight
  entries; they are not exposed at the Ethernet top.
- Memory preferences are selected during RTL export, independently of capacity:
  `mill -i netstack.runMain netstack.Emit OUTPUT_DIRECTORY [hybrid|bram|auto]`.
  Default `hybrid` maps synchronous queues at least eight entries deep and
  96 bits wide to BRAM and retained 128-bit
  payload memories of at least 512 words to URAM. `bram` requests BRAM for
  both; `auto` leaves placement to synthesis. Choose `bram` or `auto` for
  devices without URAM, then reverify and close timing for that device.
  Frozen preparation accepts the same choice:
  `python3 fpga/prepare_registered_ooc.py --memory-profile bram` after
  emitting `build` with `bram`. The preparer re-emits the requested profile,
  rejects mismatched exports and freezes the selected attributes.
  These attributes change primitive preferences, not logical capacity/latency.
  Other memories retain their existing inference. The selected profile is
  written to each export directory and frozen into Vivado provenance.
- The multi-context receive parser is shared. Independent two-packet committed
  staging per context preserves bounded isolation from a stalled TCB. Admission
  tracks up to four packets per active context across the shared/staging
  pipeline; a listener reserves only one until validation resolves. This
  admission credit is not the application's byte-ring capacity.

`bench/configuration_probe.py` elaborates full production-storage tops with
1/3/8 contexts. Elaboration proves that configuration produces RTL, not that
every configuration passes 10G functional traffic or routed timing.
