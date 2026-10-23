# Valence-Net

Chisel hardware IPv4 TCP/UDP stack for the Valence FPGA SoC. The current
integrated endpoint supports **four independent TCP sessions by default**
(configurable from one to eight), together with shared UDP, ARP and ICMP Echo. It targets **10 Gb/s Ethernet** with a **128-bit, 156.25 MHz internal
datapath (20 Gb/s raw bandwidth)**. Functional verification, cycle service
capacity and physical timing are recorded separately.

The multi-connection/SACK revision passes **31 functional targets**, including
complete-Ethernet single/four-context RX offered at 10G and sustained
four-context TX. The default four-context core passes routed OOC timing at
**156.25 MHz on ZU15EG-2**, with **+0.004 ns setup / +0.006 ns hold**.
See [development evidence](docs/DEVELOPMENT.md) and the
[original timing report](docs/timing/2026-10-23-113414/post_route_timing.rpt).
SoC integration and board throughput remain separate validation steps.

Develop in WSL at **/home/openion/Valence-Net**. Source, Git, Mill/GSIM builds
and caches stay on the Linux filesystem. Explicitly use WSL's `--cd` option
when invoking tools from Windows. Windows Vivado uses frozen snapshots under
**E:\VM\Share\Valence-rtl**.

## Integrated design

`MultiEthernetTcpUdpEndpoint` combines:

- Ethernet-II receive classification, complete-frame validation, header
  removal, minimum-frame transmit padding and stable frame arbitration.
- IPv4 header/length/checksum/destination validation and unfragmented transmit.
- ARP cache, local-address replies, bounded resolution retries/expiry,
  connected-subnet/gateway routing, MTU checks and address-conflict indication.
- Bidirectional UDP with length, checksum, metadata and empty-payload handling.
- Independent active/passive/simultaneous-open TCP sessions, all eleven connection states,
  MSS/window-scale negotiation, durable receive bytes, bounded out-of-order
  retention/reassembly, overlap trimming,
  sequence-wrap-aware ACK processing, half-close and TIME-WAIT.
- Per-connection retained TCP TX banks, segmentation, cumulative-ACK reclamation
  and arbitrary partial-ACK replay.
- SACK-permitted negotiation, up to four receiver SACK blocks from committed
  reorder storage, and bounded selective hole retransmission. Only cumulative
  ACKs reclaim payload. RTO discards advisory SACK state for reneging recovery.
- Shared tuple dispatch, same-port passive listeners, fair packet-level TCP TX
  arbitration and a global physical-completion ledger that maps results back
  to each connection's generation and local tag.
- RFC 6298 RTT/RTO estimation, Karn exclusion, exponential backoff, SYN fallback
  and bounded completed loss retransmissions.
- Autonomous conservative Reno: one-SMSS initial/restart window, byte-counted
  slow start/congestion avoidance, three-duplicate-ACK fast retransmit,
  fast recovery and timeout loss response.
- Zero-window persist probes with completion-gated exponential scheduling,
  independent of the loss retry budget.
- ICMPv4 unicast Echo response after complete IP/ICMP checksum validation.

Raw IP integration is available as `MultiTcpIpv4Endpoint`. The compatible
single-session `EthernetTcpUdpEndpoint` and `TcpIpv4Endpoint` remain available.
Smaller independently
tested blocks remain usable: `TcpConnectionControl`,
`TcpManagedConnection`, `TcpConnectionTransmit`, `TcpDataTransmit`,
`TcpTransmitQueue`, `TcpPayloadStore`, `TcpReceiveQueue`, `TcpReorderBuffer`,
`TcpReceiveRing`, `TcpReceive`, `Ipv4TcpTransmit`, `EthernetUdpEndpoint`,
`Ipv4UdpReceive/Transmit`, `ArpEndpoint`, packet buffers/slices/prependers and
`InternetChecksum`.

See [integration contracts](docs/INTEGRATION.md) for MAC completions, application
streams and the proposed Linux integration boundary. SoC adapters remain a
separate task.

## Stream and ownership contracts

Byte zero occupies bits 7:0. Nonfinal packet beats are full; final `keep` is
a contiguous low-bit prefix. An empty application write/datagram uses one
`keep=0,last=true` beat. Values of an offered output remain stable while
`valid && !ready`.

MAC-facing frames exclude preamble/SFD, FCS and IFG. The MAC adapter handles
these fields and buffers or drops complete ingress frames: the physical wire
cannot honor internal backpressure. A late receive error cannot expose any
bytes from a rejected frame.

The multi-connection application interface is `connections(i)`: the vector
index is a stable elaboration-time connection ID, with independent streams,
backpressure and notices. TCP application writes consist of allocation, packed bytes, then a store-result
handshake. A write is at most one retained bank; larger streams use consecutive
writes. Invalid/empty writes allocate no sequence space. Bank ownership lasts
until its scheduled bytes are cumulatively ACKed. A pinned replay remains safe
even if its descriptor is ACKed and retired while output is stalled.

TCP receive output is an ordered **byte stream**. Each output beat's `last`
delimits a chunk; it is not a peer segment boundary or FIN. Connection notices
report establishment, peer FIN, normal close, reset and abort. Read already
buffered data before treating peer FIN as application EOF. SYN-carried bytes
are hidden until establishment. Normal close preserves unread bytes; reset or
abort cancels them. Reopening waits for old RX ownership and TX completions to
drain.

Every submitted TCP IP packet, including ACK-only/probe packets, has an ordered
tag/generation ledger entry. `txTcp/txTag/txGeneration` identify TCP Ethernet
frames; `routed` reports route success/drop. The MAC adapter returns exactly
one `completed` result per TCP submission, in order, echoing tag and generation.
It must report failure for a routing drop or failed physical transmission.

**IP/MAC queue acceptance does not mean physical transmission.** Only the
adapter's actual physical completion can advance transmitted sequence state
or arm the loss timer. A completion is accepted starting the cycle after its
IP packet's final handshake. Unknown, duplicate, out-of-order and stale results
cannot free ledger entries. Route/MAC failure aborts; an unsent sequence range
is never reported as transmitted. Reset must also reset the adapter's ledger.
An already offered frame remains stable through abort and drains to completion.

Local MAC/IP/netmask/gateway/MTU are configured while quiescent. TCP local MSS
must fit the configured IP MTU and the application's intended path. ISNs and
passive listener filters are explicit open inputs. The default autonomous
congestion policy is enabled at integrated endpoint level; `congestionWindow`
is an additional upper cap. Raw scheduler tests can disable the policy.

## Capacity and measured service budget

Default storage **per connection** is **8 x 2 KiB = 16 KiB** retained TX,
**64 KiB** RX and a separate **8 x 2 KiB** receive reorder cache. Four connections
therefore have 384 KiB of these payload memories, plus packet buffers and
metadata. Packet capacity is 128 beats; each connection has 16 completion
entries, and the shared ledger has 64 entries at the default four connections. Parameters are elaboration-time choices. ARP defaults to
16 cache entries, one-second retries, three attempts and 60-second lifetime.
A tick is one millisecond by default. Initial/minimum RTO is one second,
maximum 60 seconds; TIME-WAIT is 120 seconds.

The TX window must cover bandwidth times RTT. At 10 Gb/s, 16 KiB covers about
**13 microseconds** of flight, before peer/congestion limits. For longer RTTs,
increase retained capacity or integrate external storage. Many one-byte
application writes also consume one bank apiece: the four-bank stress
geometry reaches 9.36 Gb/s in that test, while the production eight-bank
geometry meets the 10G service budget.

The multi-context Ethernet harness uses four TX banks and a 2 KiB RX ring per
context. Sustained four-context TX checks 256 frames with independent physical
callbacks and peer ACKs: **9 clocks/frame for 1 byte** and **100 clocks/frame for
1460 bytes** (11.667 and 19.225 Gb/s wire-equivalent scheduling capacity).
Single/four-context RX meets exact 10G deadlines for 256 pure ACKs, 4096
one-byte packets and 256 1460-byte packets, with zero drops and every payload
byte checked. Final receive-window updates and zero-window reopening are checked.
These measurements use cached ARP and consumers that keep up, with reduced
harness storage; routed timing uses full production storage.

The current-source regression rechecks the compatible single-session top.
`ethernet_service` checks complete Ethernet/IP/transport bytes, uses eight
TX banks, autonomous Reno, cached ARP, prompt modeled physical completions/
peer ACKs and ready consumers. It measures 256 packets per case, excluding
startup/drain and the first 32 completion intervals.

| Path | Payload bytes | Cycles/packet | Wire-equivalent capacity at 156.25 MHz |
| --- | ---: | ---: | ---: |
| UDP TX | 0 / 1 / 17 | 9 | 11.667 Gb/s |
| UDP RX | 0 / 1 / 17 | 7 | 15.000 Gb/s |
| UDP TX / RX | 512 | 40 / 38 | 18.063 / 19.013 Gb/s |
| UDP TX / RX | 1400 | 96 / 94 | 19.089 / 19.495 Gb/s |
| TCP TX | 1 | 9 | 11.667 Gb/s |
| TCP TX | 64 | 13 | 13.654 Gb/s |
| TCP TX | 1460 | 100 | 19.225 Gb/s |
| TCP RX | 1 | 9 | 11.667 Gb/s |

The current-source production-capacity probe
(`build/production-rx-probe-20261023T114045Z`) checks the compatible single-session
Ethernet top. `python3 bench/production_rx_probe.py` uses an isolated source copy
and enlarges only the service harness RX ring to
the production 64 KiB capacity. TCP RX at 1460 bytes measures **98 cycles per
packet / 19.617 Gb/s wire-equivalent**. The probe retains the harness's reduced
ARP/completion/timer geometry, records its own source/model/result hashes,
and leaves authoritative sources and the full-regression manifest untouched.

A companion `bench/production_reassembly_probe.py PROBE_DIRECTORY` reuses
the verified 64 KiB model and checks 24 shuffled waves of eight retained
1460-byte gaps followed by a missing prefix: 315,360 ordered bytes, with
consumer stalls, sequence wrap and independent checksum checks. These
reassembly scenarios are correctness checks, not a sustained disorder-rate
throughput claim. MAC buffering must also absorb internal replay bursts.

Wire-equivalent conversion includes Ethernet header/padding/FCS, preamble/SFD
and IFG. These results show scheduling capacity under the stated conditions;
physical clock closure and board throughput require separate evidence.
The 20G figure describes raw internal width-times-clock bandwidth.

## Build and verification

Dependencies: Mill 1.1.7, Scala 2.13.17, Chisel 7.3.0, Clang 19+ and GSIM
revision `93b8cd23edd3228807c4f2a08c19c3936a463cb2`. The default GSIM
checkout is read from /home/openion/Valence/simulator/build/gsim-src;
override `GSIM_SOURCE` and `GSIM_CXX` for another installation. The build
does not modify that checkout and does not require Verilator.

```sh
cd /home/openion/Valence-Net
python3 sim/run.py
# Focused development regressions:
python3 sim/run.py tcp_control tcp_managed tcp_data tcp_connection_tx
python3 sim/run.py tcp_ring tcp_endpoint tcp_reassembly tcp_congestion tcp_congested_endpoint
python3 sim/run.py ethernet_tcp ethernet_mixed ethernet_service
python3 sim/run.py tcp_header_snapshot tcp_sack tcp_multi ethernet_multi ethernet_multi_rx
```

The runner emits FIRRTL/SystemVerilog, generates GSIM models, compiles C++20
software peers with ASan/UBSan and writes logs plus a source-hashed manifest
to `build/verified.json`. A failed run removes the previous manifest.
A focused run records only the selected tests; old logs are never promoted
to evidence for new sources. Editing source while tests run invalidates
the result. Default and reduced test geometries are emitted into separate
RTL directories so shared module names cannot silently replace one another.

Wire peers cover active/passive opening, SYN data, wraparound, partial ACK,
loss/replay, zero windows, fast recovery, ARP exhaustion, normal close with
unread data, abort while output is stalled, old/duplicate completions, reset,
mixed TCP/UDP/ICMP traffic and independent backpressure. Reassembly checks
cover shuffled gaps, overlapping ranges, capacity exhaustion, sequence wrap,
window clipping, ordered FIN, invalid admission, abort/reuse and old-ACK
isolation from Reno. Tests use independent
packet/checksum/byte expectations. They are not a claim of exhaustive TCP
compliance or interoperability with a physical host.

The pinned GSIM constant-concatenation issue and equivalent wide-field
placement are documented in `sim/gsim-known-issues.txt`.

## Vivado timing

Target part: **xczu15eg-ffvb1156-2-i**. Clock: **6.4 ns**. Setup/hold
uncertainty: **0.1 / 0.05 ns**. No false-path or multicycle exceptions are used.

Snapshot **20261023T113414Z** completed route and physical refinement with
zero failing setup/hold endpoints, clean timing coverage, zero routing errors
and zero DRC errors. All **223,179 routable nets** are fully routed. The core
uses **172,027 LUTs, 81,490 FFs, 132 RAMB36 and 18 RAMB18**, excluding the timing
fixture. Published [summary](docs/timing/2026-10-23-113414/summary.json),
[report hashes](docs/timing/2026-10-23-113414/evidence-hashes.json) and
[warning review](docs/timing/2026-10-23-113414/tool-log-review.json) correspond
to the same source as the 31-target functional regression.

`fpga/prepare_ooc.py` freezes source verification, default RTL, hashes,
constraints and script into a timestamped native-WSL snapshot.
`fpga/prepare_registered_ooc.py` adds a static-timing fixture around the
unchanged default four-connection `MultiEthernetTcpUdpEndpoint`: retained input/output registers and
a physical BUFG provide concrete boundary paths. Its random stimuli are
a timing load, not a functional protocol test. Reports separately identify
fixture and core utilization.

Direct OOC runs also retain their reports, but their unconstrained physical
partition-pin locations make external-boundary routing/hold results unsuitable
for SoC signoff. The registered fixture measures the core with explicit
register boundaries; its observation signature is not a board interface.

```sh
python3 fpga/prepare_registered_ooc.py
# Explicit legacy single-connection timing fixture:
python3 fpga/prepare_registered_ooc.py --core EthernetTcpUdpEndpoint
# Or a focused synthesis snapshot:
python3 fpga/prepare_ooc.py TcpManagedConnection
```

In Windows PowerShell, invoke `fpga/run_windows.ps1` with the snapshot UNC
path and `-Mode Run -Phase route` (or `synth`). The default executable is
Vivado 2025.2; `-VivadoPath` overrides it. If needed, use a process-scoped
`-ExecutionPolicy Bypass`. Inputs/results live in a fresh
`E:\VM\Share\Valence-rtl\hardware-netstack-ooc-TIMESTAMP` directory.
`Collect` copies completed results back into the WSL snapshot. The launcher
refuses to overwrite an existing run.

Retain setup, hold, pulse-width, timing coverage, routing status, utilization,
DRC and methodology reports. `fpga/summarize_ooc.py` audits frozen hashes
and distinguishes synth-only from routed evidence. A core/fixture result
does not establish integrated SoC, CDC, MAC/PHY or board timing. Current run
history and incomplete stages are recorded in `docs/DEVELOPMENT.md`.

## Scope boundaries

This revision supports 1–8 parallel TCP contexts and one local IPv4
configuration. IPv4 IHL=5 and unfragmented traffic are supported. Receive gaps
are retained in eight bounded reorder slots per connection. Cache overflow,
overlapping cached ranges and oversized ranges are dropped for sender
retransmission; the missing prefix can still enter a full cache. Unknown tuples
and busy receive contexts are silently dropped; there is no listening backlog
or automatic context allocator beyond the explicitly opened listener slots.

SACK recovery uses a bounded advisory scoreboard and ACK-clocked conservative
Reno recovery. It is **not a complete RFC 6675 Pipe/IsLost/rescue algorithm**.
RX SACK snapshots are built over registered steps after payload commitment;
an initial gap ACK can precede the subsequent SACK update. Further out-of-order
metadata can wait for that snapshot; this is not a sustained disorder-rate
line-rate claim. SACK does not increase retained capacity or eliminate RTO.
NewReno/ECN, DSACK, delayed ACK aggregation, timestamps negotiation, IPv6,
IP options/fragments, VLAN, multicast membership and dynamic routing remain
outside this implementation.

ICMP support currently covers Echo; ICMP errors/PMTU feedback are not
implemented. TCP TTL/TOS currently use internal defaults. A secure autonomous
ISN policy and configurable connection user-timeouts are future work.

Physical MAC/PCS/PHY integration, DMA/Linux interfaces and board peer tests
are separate integration work. These boundaries and the timing evidence must
be preserved when describing the implementation's capabilities.

Protocol references: [TCP](https://www.rfc-editor.org/rfc/rfc9293),
[Reno](https://www.rfc-editor.org/rfc/rfc5681),
[SACK](https://www.rfc-editor.org/rfc/rfc2018),
[SACK recovery reference](https://www.rfc-editor.org/rfc/rfc6675),
[RTO](https://www.rfc-editor.org/rfc/rfc6298),
[ARP](https://www.rfc-editor.org/rfc/rfc826),
[IPv4](https://www.rfc-editor.org/rfc/rfc791),
[UDP](https://www.rfc-editor.org/rfc/rfc768),
[ICMP](https://www.rfc-editor.org/rfc/rfc792).
