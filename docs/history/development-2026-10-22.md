# Valence-Net development evidence (2026-10-22)

Target: 10 Gb/s Ethernet; 128 bits at 156.25 MHz provides 20 Gb/s raw
internal bandwidth. Functional correctness, scheduling capacity and physical
timing have separate evidence.

Source, Git, caches and simulation stay in native WSL at
/home/openion/Valence-Net (Ubuntu-24.04, user openion). Invoke WSL with an
explicit native --cd. Frozen Windows Vivado runs use
E:\VM\Share\Valence-rtl. The checkout base is
54d589cb212dcdb812cfb5fb34e342314fbddb38.

## Current scope and receive reassembly

EthernetTcpUdpEndpoint integrates one TCP session, UDP, ARP, unfragmented
IPv4 and ICMP Echo. TCP provides active/passive/simultaneous opening, retained
TX segmentation, cumulative/partial ACK reclamation, RTT/RTO and Karn handling,
Reno, persist, half-close and TIME-WAIT. Physical completions are explicit and
ordered; queue acceptance never starts a transmission timer. Normal close
preserves unread receive bytes; reset/abort cancels ownership.

This revision adds TcpReorderBuffer to TcpReceiveQueue:

- Eight separate 2 KiB banks retain validated future segments. Range admission
  follows tuple, sequence, ACK, checksum and state validation.
- A missing prefix keeps cumulative ACK at the gap. When it arrives, retained
  ranges are copied into the application ring in sequence order, trimming
  already received prefixes. FIN also waits for its sequence position.
- Overlapping cached ranges, cache exhaustion and oversized ranges drop the
  new segment for sender retransmission. A full cache never blocks a missing
  prefix. This version does not negotiate SACK.
- The cache does not reduce the ring's advertised credit twice. Promotion
  reserves the ring capacity. These extra banks add no advertised window.
- Internal replay applies only receive data/FIN processing. It never replays
  the segment's old ACK, sender-window or congestion events.
- Generation checks and drain-before-reclaim ownership prevent reset/abort
  from leaking stale data into a reopened connection.

Production storage: 16 KiB retained TX, 64 KiB application RX, 16 KiB reorder
cache, and 16 physical-completion ledger entries. At 10 Gb/s the TX capacity
covers only about 13 microseconds of flight; longer RTTs need more storage.
SYN-carried data remains hidden until establishment. Read buffered bytes before
interpreting a peer-FIN notice as application EOF.

## Functional evidence

The complete 26-target regression passes using the exact 69-file source set
in build/verified.json. Independent C++ peers use Clang 19.1.1 with ASan/UBSan.
Build versions are Mill 1.1.7, Scala 2.13.17 and Chisel 7.3.0; GSIM is pinned
to 93b8cd23edd3228807c4f2a08c19c3936a463cb2.

The new tcp_reassembly target checks 46 cases, 61,280 cycles and 14,742 bytes:
shuffled arrivals, sequence wrap, duplicate/overlap admission, trimming beyond
127 bytes, full-cache recovery, window-edge clipping and zero windows,
data-bearing/empty FIN, invalid ACK/tuple/checksum/stream-error rejection,
abort during copy/promotion, generation reuse, and isolation of old ACKs from
Reno. Randomized application/network/control backpressure is enabled.

Existing packet, checksum, ring, retained-store, timer, control, integrated
TCP, congestion, mixed TCP/UDP/ICMP and service tests also pass. Selected
independent checks include 2,788,617 RX-ring bytes, 4,801 retained-store replays,
all 11 connection states, partial-ACK retransmission, stalled abort outputs,
old/duplicate completion callbacks and route/MAC failures.

A separate production-capacity probe at
build/production-rx-probe-20261022T150556Z uses the production 64 KiB receive
ring while retaining the harness's reduced ARP/completion/timer settings.
Its companion reassembly probe checks 24 waves of eight retained 1460-byte
segments followed by a missing prefix: 315,360 bytes across 130,923 cycles,
with sequence wrap, shuffled arrivals and a stalled consumer.

The full regression and both isolated probes have matching authoritative
source hashes. Probe scripts preserve root sources and the full manifest.
Their model, inputs, commands, sanitizer settings and results are recorded.
These are independent byte/checksum checks, not board interoperability tests.

## Scheduling capacity

The full-endpoint service benchmark uses eight TX banks, enabled Reno, cached
ARP, prompt modeled physical completions/peer ACKs and ready consumers.
Each case validates 256 packets and excludes startup/drain and the first
32 completion intervals.

| Path / payload | Cycles per packet | Wire-equivalent Gb/s at 156.25 MHz |
| --- | ---: | ---: |
| UDP TX / 0, 1 or 17 bytes | 9 | 11.667 |
| UDP RX / 0, 1 or 17 bytes | 7 | 15.000 |
| UDP TX / 1400 bytes | 96 | 19.089 |
| UDP RX / 1400 bytes | 94 | 19.495 |
| TCP TX / 1 byte | 9 | 11.667 |
| TCP TX / 64 bytes | 13 | 13.654 |
| TCP TX / 1460 bytes | 100 | 19.225 |
| TCP RX / 1 byte | 9 | 11.667 |
| TCP RX / 1460 bytes, 64 KiB ring probe | 98 | 19.617 |

Conversion includes Ethernet header/padding/FCS, preamble/SFD and IFG.
The reassembly addition preserves these ordered-traffic service rates.
The four-bank stress geometry previously reached only 9.36 Gb/s for one-byte
writes; production eight-bank geometry passes that 10G service budget.

These results do not establish sustained throughput under arbitrary
reordering, RTT, loss or backpressure. Cache replay temporarily owns the receive
copy path; the MAC adapter needs sufficient burst buffering. Physical wire
ingress cannot honor an internal ready signal.

## Routed timing evidence

Part xczu15eg-ffvb1156-2-i, Vivado 2025.2, 6.4 ns clock, setup/hold uncertainty
0.1/0.05 ns. No false-path or multicycle exceptions were introduced.
ValenceNetTimingTop supplies retained input/output registers and an actual
located BUFG around the unchanged default endpoint. Random fixture stimuli
are a static timing load, not a protocol test or board top.

| Snapshot | Revision | Setup WNS | Hold WHS | Result |
| --- | --- | ---: | ---: | --- |
| 20261022T132100Z | Full endpoint before control-path optimization | -0.542 ns | +0.010 ns | Fail |
| 20261022T134947Z | Pipelined RTO/RX and trusted reservations | -0.033 ns | +0.006 ns | Fail |
| 20261022T140720Z | Parallel ACK right edges, precomputed ISN+1 | +0.015 ns | +0.006 ns | Pass |
| 20261022T150138Z | Bounded receive reassembly | +0.001 ns | +0.003 ns | Pass |

The current snapshot has pulse-width slack +2.658 ns, zero failing timing
endpoints, zero black boxes, zero routing errors, all 57,855 routable nets
fully routed, and all timing-coverage checks zero. The worst path runs from
UDP RX IP metadata to its UDP checksum result register (21 logic levels).
Setup margin is only 1 ps beyond the stated uncertainty.

Current core utilization: 41,411 LUT, 18,183 FF, 42 RAMB36, 9 RAMB18,
no DSP/URAM. Including the fixture: 41,760 LUT and 20,778 FF.
Compared with the previous routed core, reassembly adds 5,435 LUT,
2,250 FF and four RAMB36.

Reports retain the observation-signature no-load DRC warning, 51 BRAM
output-register warnings, 51 address-collision warnings and the clock-location
advisory. There are no DRC/methodology errors.

The tool log also contains four occurrences of Designutils 20-1307 from
unsupported XDC conditionals around data-input delays. This fixture's only
input is its clock, so those data-input sets are empty; all core inputs are
driven by actual registers. Applicable clock, uncertainty and output constraints
are present and timing coverage is clean. The source helper now emits the
conditional outside XDC for subsequent runs. The completed frozen run and its
warnings remain unchanged. The audit explicitly records these warnings; this
is not described as a warning-free run.

The snapshot was frozen after the focused reassembly test. The subsequent
full 26-test manifest has the identical source hashes and is retained in the
snapshot as subsequent-full-regression.json. Fresh Chisel export bytes and
frozen RTL/script hashes pass audit. The preparer now copies directly from
the fresh export directory, excluding obsolete uninstantiated cached exports.
Reports/checkpoints were collected back into native WSL.

Earlier synthesis-only snapshots (120422Z, 121449Z, 122856Z, 124151Z, 125542Z)
remain historical diagnostics. A stopped direct-OOC route with unlocated
external partition pins is not signoff evidence. The prior optimizations
pipeline RTT-to-RTO rearm and RX events, and remove duplicate reservation
checks only inside the trusted scheduler with an invariant monitor.

## Reproduce and remaining work

Run from the native WSL project:

~~~sh
python3 sim/run.py
python3 bench/production_rx_probe.py
python3 bench/production_reassembly_probe.py build/production-rx-probe-TIMESTAMP
python3 fpga/prepare_registered_ooc.py
~~~

Run the frozen snapshot through fpga/run_windows.ps1 in Windows using
-Mode Run -Phase route, then -Mode Collect. Audit the collected snapshot with
fpga/summarize_ooc.py. See verification-2026-10-22.json for compact evidence,
and INTEGRATION.md for stream, completion and receive ownership contracts.

Remaining work includes improving the narrow timing margin, multiple TCP
sessions, SACK/NewReno/ECN, ICMP errors/PMTU, configurable user timeouts and
secure autonomous ISNs. IPv6, IP fragments/options and VLAN are absent.
MAC/PCS/PHY, CDC, DMA/Linux interfaces and real-peer board measurements remain
integration work. Repeat timing after actual SoC integration.

The current result qualifies the registered-boundary core at the target clock;
it does not establish SoC timing, board throughput or a complete general-purpose
TCP/IP stack. No subagents were used.
