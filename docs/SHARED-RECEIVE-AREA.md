# Shared receive and memory mapping, 2026-10-25

This round starts at commit `57c6984`. It keeps four default contexts, 16 KiB
TX retention, 64 KiB RX and eight 2 KiB reorder banks per context. Packet
capacity, SACK, ICMP/PMTU and the 128-bit interface are retained. The target is
156.25 MHz on xczu15eg-ffvb1156-2-i with 0.100/0.050 ns setup/hold uncertainty.
SoC adaptation remains outside this task.

## Shared work and independent ownership

One complete IPv4/TCP receive validator replaces the four context-local
validators and the stateless responder's separate validation. The tuple
classifier attaches an immutable slot/generation tag. Packet-bank commits,
header slicing and checksum/format rejection preserve that tag.

Each context retains two committed staging banks before its TCB. This bounded
decoupling is necessary: a direct shared-validator-to-TCB connection failed the
existing busy-context isolation oracle. A slow context must not hold another
context's metadata waiting for an application notice. Stale generations drain
locally and retire once; reopening waits for all prior receive ownership.
Active contexts have four admission credits across the combined pipeline;
listeners admit one until validation resolves. These credits are separate
from application receive-byte capacity. The extra committed staging hop adds
receive latency; the service measurements below certify throughput, not
identical end-to-end packet latency.

The TCBs, flow control, receive rings, reorder ownership, retransmission state,
congestion policies and local completion ledgers remain independent.
Stateless resets use shared validated metadata and recheck current ownership,
including a simultaneous OPEN, before responding.

## Memory policy

Retained TX and receive-reorder payload memories use URAM in the default
`hybrid` export. Synchronous queues at least eight entries deep and 96 bits
wide prefer BRAM. The collision-safe FIFO prefetches its next head and forwards
same-address writes explicitly; it preserves Queue's non-flowing,
non-pipelined handshakes.

Shallow queues retain ordinary inference. An all-wide-BRAM trial grew control
serializers from about 1,837 to 3,218–3,408 LUTs each and left only 3.93% net
synthesis savings. The selective policy restores them to 1,836 LUTs each.
The trial differs in one source file and its original report/provenance are
retained under [the memory-policy experiment](experiments/2026-10-25-memory-policy/evidence.json).
It was stopped after synthesis and has no routed timing qualification.

Chisel exports named synchronous arrays. `MemoryExport` deterministically adds
RAM_STYLE attributes to those arrays; it changes no logic, latency or ports.
The profile probe checks identical RTL after attribute removal and repeated
export. `hybrid / bram / auto` choices and the selected profile are recorded
in the frozen provenance. See [parameters](PARAMETERS.md).

## Functional and service evidence

The exact 111-file source set passes 46 ASan/UBSan targets. All 44 pre-existing
result strings are identical to the baseline, with their packet/checksum/byte
oracles unchanged. New checks cover:

- 500,000 FIFO cycles, including empty writes, simultaneous transfers,
  wraparound, resets and long stalled heads at depths 2/3/16.
- 1,200 interleaved tagged IPv4/TCP packets: 450 valid, 750 rejected, with
  poisoned subsequent-beat tags, IPv4/TCP checksum failures, fragments, stream
  errors, non-TCP protocol drains and output stalls.

The RAM primitive experiments also pass Vivado post-synthesis functional
simulation: 50,000 FIFO cycles and 10,000 URAM cycles. The FIFO harness explicitly
forces synchronous RAM at all three tested depths. URAM tests use disjoint
concurrent read/write addresses, as retained-bank ownership requires.
[Original netlists, logs and hashes](memory/2026-10-25/evidence.json) are retained.

The production-capacity multi-context probe uses eight TX banks, a 64 KiB RX
ring, 16 local completion entries and ARP16. It widens both external and
internal test-only credit interfaces; a three-bit bank count cannot represent
eight free banks. The existing independent TX/RX oracles run with only their
capacity-specific expectations adapted.

Single/four-context complete-Ethernet RX meets every exact 10G offered-load
deadline: pure ACK and one-byte packets at 10.5 clocks/frame; 1460-byte payloads
at 192.25 clocks/frame. There are zero drops and every payload byte is checked.
The production ring is filled to zero window, then partial and full reopening
are verified. Four-context TX remains 9 clocks/frame for one-byte payloads and
100 for 1460-byte payloads, with 256 frames, independent completions, peer ACKs
and all eight banks reclaimed.

Full 1/2/3/8-context configurations elaborate without warnings; these parameter
probes are elaboration evidence, not timing certification for those geometries.
[Full regression, production probes and configuration hashes](verification/2026-10-25-shared/evidence.json)
bind these results to the same sources as the frozen Vivado input. The initial
CAD snapshot contains five focused tests; the later complete 46-target manifest
has an identical source-hash map and is archived separately.

## Same-stage synthesis comparison

| Core resource | Previous revision | Shared receive + selective RAM |
| --- | ---: | ---: |
| Total LUTs | 181,021 | 164,636 |
| Logic LUTs | 171,573 | 155,756 |
| LUTRAM LUTs | 9,448 | 8,880 |
| FFs | 90,378 | 86,461 |
| RAMB36 / RAMB18 | 136 / 20 | 109 / 19 |
| URAM / DSP | 0 / 0 | 16 / 0 |

The net synthesis reduction is **16,385 LUTs / 9.051%** and 3,917 FFs.
URAM replaces retained payload BRAM; sharing removes duplicate parsers.
Unused physical RAM capacity is not a reduction in logical buffering.
Forcing every small FIFO into BRAM was counterproductive, as the trial shows.

## Physical qualification

The default-geometry run `20261025T082252Z` completes routed signoff after
local `AggressiveExplore` physical optimization. Its initial fully routed
checkpoint has setup -0.164 ns / hold +0.005 ns. Local optimization reaches
zero failing endpoints without globally ripping up the completed route.
The flow saves the local checkpoint and checks routing before considering
any global reroute; this run needs none.

Final setup/hold both display **0.000 ns**, with **TNS/THS 0 and zero failing
endpoints** over 252,550 setup and hold endpoints. Pulse-width slack is
+2.500 ns. All 222,523 routable nets are fully routed; routing errors,
blackboxes, DRC/methodology errors and all twelve timing-coverage counts are
zero. Signoff remains **6.4 / 0.100 / 0.050 ns**, with no false-path or multicycle
exceptions. The rounded margins are not the pass criterion: endpoint failure
counts and negative-slack totals are checked independently.

| Fully routed core resource | Previous revision | Shared receive + selective RAM |
| --- | ---: | ---: |
| Total LUTs | 180,442 | 164,239 |
| Logic LUTs | 171,022 | 155,379 |
| LUTRAM LUTs | 9,420 | 8,860 |
| FFs | 90,361 | 86,415 |
| RAMB36 / RAMB18 | 136 / 20 | 109 / 19 |
| URAM / DSP | 0 / 0 | 16 / 0 |

This is **16,203 fewer LUTs / 8.980%** and 3,946 fewer FFs at the same routed
stage. Across both area rounds, the earlier 184,655-LUT exception/PMTU core
falls by 20,416 LUTs / 11.056%. No connection or buffer capacity was reduced.

[Original reports and hashes](timing/2026-10-25-082252/artifact-hashes.json),
[source qualification](timing/2026-10-25-082252/source-qualification.json) and
[combined source/area record](verification-2026-10-25-shared-area.json) bind the
same source hashes to functional, production-service and physical results.
Warnings are reviewed, not erased: large register-query warnings, RAM
collision/output-register advisories, the fixed clock-buffer site and the
fixture's unloaded signature are retained. A console-decoding fixture verifies
that UTF-8 and UTF-16 error messages prevent qualification even when numeric
timing is clean.

Both setup and hold have little additional margin after uncertainty. A new
SoC/MAC/board integration needs its own timing closure. The previous routed
area revision remains [documented separately](AREA-OPTIMIZATION.md).

## Protocol scope

This is a bounded IPv4 TCP/UDP core, not a complete general-purpose stack.
[The feature/limit matrix](PROTOCOL-STATUS.md) distinguishes implemented
reassembly, bounded SACK and ICMP/PMTU from missing IPv6, IP fragmentation,
full RFC 6675 recovery and PLPMTUD. No SoC, MAC/PHY or physical-peer throughput
claim follows from these simulations or OOC reports.
