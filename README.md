# Valence-Net

Chisel hardware IPv4 TCP/UDP stack for the Valence FPGA SoC. The current
integrated endpoint supports **four independent TCP sessions by default**
(configurable from one to eight), together with shared UDP, ARP, ICMP Echo/error handling and path-MTU feedback. It targets **10 Gb/s Ethernet** with a **128-bit, 156.25 MHz internal
datapath (20 Gb/s raw bandwidth)**. Functional verification, cycle service
capacity and physical timing are recorded separately.

The current shared-receive revision passes **46 functional targets** with unchanged
default capacity. The fully routed core falls from **180,442 to 164,239 LUTs
(8.980%)**, uses 16 URAM blocks and meets **156.25 MHz OOC on ZU15EG-2**.
Setup/hold failing endpoints and TNS/THS are zero; both displayed margins are
**0.000 ns** after the original 0.100/0.050 ns uncertainties, so integration
margin remains thin. The exact 111-file source set also passes production-capacity
single/four-context 10G RX and four-context TX checks. See
[shared receive / RAM evidence](docs/SHARED-RECEIVE-AREA.md),
[routed qualification](docs/timing/2026-10-25-082252/source-qualification.json)
and [elaboration parameters](docs/PARAMETERS.md).

The earlier exception/PMTU revision passes **42 functional targets**, including
complete-Ethernet single/four-context RX offered at 10G and sustained
four-context TX. Its exact 101-file source set matches the routed OOC result:
**156.25 MHz on ZU15EG-2, setup +0.001 ns / hold +0.006 ns**, with zero failing
endpoints and complete routing. See the [earlier timing report](docs/timing/2026-10-24-042645/post_route_timing.rpt)
and [source qualification](docs/timing/2026-10-24-042645/source-qualification.json).

The earlier 31-target multi-connection/SACK baseline passed routed OOC timing
at **156.25 MHz on ZU15EG-2**, with **+0.004 ns setup / +0.006 ns hold**.
That historical margin does not certify the current sources.
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
- Shared tuple dispatch and complete IPv4/TCP receive validation with immutable
  slot/generation tags, independent committed per-context staging, same-port
  passive listeners, fair packet-level TCP TX
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
- Bounded, rate-limited TCP resets for unowned/closed tuples, with complete
  IPv4/TCP validation and isolation from busy or still-reserved connections.
- ICMP protocol-unreachable replies and optional UDP port-unreachable replies;
  validated destination-unreachable, time-exceeded and parameter-problem
  feedback for locally originated TCP/UDP traffic.
- Per-connection TCP path-MTU reduction and retained-data repair, plus bounded
  UDP destination-MTU caching and application-visible oversized-datagram errors.

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

Every connection-owned TCP IP packet, including ACK-only/probe packets, has
an ordered tag/generation ledger entry. `txTcp/txTag/txGeneration` identify
those Ethernet frames; `routed` reports route success/drop. The MAC adapter
returns exactly one `completed` result per tagged TCP submission, in order,
echoing tag and generation. It must report failure for a routing drop or
failed physical transmission.

Stateless TCP resets use a separate exception path and have `txTcp=false`,
just like UDP/ICMP/ARP frames. They allocate no connection ledger entry and
must not receive a TCP `completed` callback, even though their IP protocol is
TCP. Raw-IP tops expose `exceptionOut/exceptionDestinationIp` separately from
the connection-owned `out/packet` stream.

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

## Exception and path-MTU behavior

Unknown TCP tuples can generate a reset only after full packet validation.
An incoming ACK produces RST with its ACK as the response sequence; otherwise
RST|ACK acknowledges payload plus SYN/FIN sequence space, including wrap.
Incoming RST, invalid packets and prohibited broadcast/multicast addresses
produce no response. Busy/reserved tuples remain silent; an unavailable
listener is not treated as a closed port. The responder drains excess packets
when its bounded storage or tick-based response budget is exhausted.

The Ethernet tops suppress replies to link broadcasts. ICMP errors quote the
original IPv4 header and up to eight payload bytes, excluding Ethernet padding.
Unknown IP protocols can produce type 3/code 2. An elaboration-time
`udpPorts` list enables UDP port filtering and type 3/code 3 replies for other
ports, after UDP length/checksum validation. The default empty list retains
accept-all UDP delivery. Responses are bounded and rate limited; error traffic
does not have a line-rate response guarantee.

Received ICMP types 3, 11 and 12 are advisory. Outer IP/ICMP and quoted IP
checksums, supported lengths and unicast addresses are validated. TCP also
checks the current four-tuple, generation and quoted sequence against
physically completed, unacknowledged flight. This deliberately excludes an
ACK-only quote at SND.NXT with no eligible unacknowledged sequence space;
an idle sender cannot learn PMTU from that quote. An accepted fragmentation-needed
report reduces only that connection's path MTU, within the configured link
limit and IPv4 minimum of 68 bytes. Zero-MTU reports use lower standard
plateaus anchored to the quoted datagram size; repeats cannot walk the same
quote down the table.

TCP applies the limit to new segments and repairs retained data without
allocating new sequence space. Cumulative ACKs still reclaim storage. An
already offered packet remains stable; subsequent repairs use the new limit,
including applicable SACK/control-header budgets. PMTU repair does not itself
consume the timeout-loss retry budget. A ten-minute path entry can return to
the link ceiling only when no sent or reserved sequence space remains in flight.
Negotiated peer MSS remains an independent upper bound.

UDP uses four destination entries and eight recent successfully routed
datagram identities by default. Feedback must match the tuple, IP ID/length,
and quoted UDP length/checksum. History expires after 60 seconds and learned
limits after ten minutes; `flushArp` also clears this path state. Route success
is not a UDP physical-completion claim. A datagram above the selected MTU is
accepted, fully drained and reported locally, with no transmitted fragment.

`connections(i).pathMtu/networkError` (or single-session
`pathMtu/networkError`) and `udpNetworkError` expose path state and notices.
Error notices are `Valid` pulses: the consumer must capture them; they provide
no backpressure or lossless event queue. A simultaneous local UDP rejection
takes precedence over an advisory. Other accepted ICMP errors do not
automatically abort TCP. ICMP quotations are not authenticated, and this
implementation does not provide black-hole detection or packetization-layer
PMTU probing. Raw-IP integrations must enforce link-layer broadcast policy and
supply only validated `icmpError` events.

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

The current multi-context Ethernet harness uses four TX banks and a 2 KiB RX ring per
context. Sustained four-context TX checks 256 frames with independent physical
callbacks and peer ACKs: **9 clocks/frame for 1 byte** and **100 clocks/frame for
1460 bytes** (11.667 and 19.225 Gb/s wire-equivalent scheduling capacity).
Single/four-context RX meets exact 10G deadlines for 256 pure ACKs, 4096
one-byte packets and 256 1460-byte packets, with zero drops and every payload
byte checked. Final receive-window updates and zero-window reopening are checked.
These measurements use cached ARP and consumers that keep up, with reduced
harness storage; routed timing uses full production storage.

The current 46-target regression also rechecks the compatible single-session top.
`ethernet_service` checks complete Ethernet/IP/transport bytes, uses eight
TX banks, autonomous Reno, cached ARP, prompt modeled physical completions/
peer ACKs and ready consumers. It measures 256 packets per case, excluding
startup/drain and the first 32 completion intervals.

| Path | Payload bytes | Cycles/packet | Wire-equivalent capacity at 156.25 MHz |
| --- | ---: | ---: | ---: |
| UDP TX | 0 / 1 / 17 | 10 | 10.500 Gb/s |
| UDP RX | 0 / 1 / 17 | 7 | 15.000 Gb/s |
| UDP TX / RX | 512 | 41 / 38 | 17.622 / 19.013 Gb/s |
| UDP TX / RX | 1400 | 97 / 94 | 18.892 / 19.495 Gb/s |
| TCP TX | 1 | 9 | 11.667 Gb/s |
| TCP TX | 64 | 13 | 13.654 Gb/s |
| TCP TX | 1460 | 100 | 19.225 Gb/s |
| TCP RX | 1 | 9 | 11.667 Gb/s |

UDP TX now waits for the ordered route result used to commit its PMTU history,
adding one service clock per datagram versus the historical baseline. The
smallest case still meets the 10G service budget at the target clock. These are
scheduling measurements; matching routed clock closure is recorded below.

The current `bench/multi_production_rx_probe.py` repeats the independent
multi-context RX/TX oracles with eight TX banks, 64 KiB RX/context, ARP16 and
16 completion entries/context. It verifies zero-window reopening, all exact
single/four-context 10G RX deadlines and every payload byte. Four-context TX
retains 9/100 clocks per one-byte/1460-byte frame and reclaims every bank.
[Source, model and result hashes](docs/verification/2026-10-25-shared/evidence.json)
match the current 111-file source set.

The previous single-session production-capacity probe
(`build/production-rx-probe-20261024T061221Z`) checks the compatible single-session
Ethernet top. `python3 bench/production_rx_probe.py` uses an isolated source copy
and enlarges only the service harness RX ring to the production 64 KiB capacity.
TCP RX at 1460 bytes measures **98 cycles per packet / 19.617 Gb/s wire-equivalent**.
The probe retains the harness's reduced ARP/completion/timer geometry, records
its own source/model/result hashes, and leaves authoritative sources and the
full-regression manifest untouched. Its frozen source hashes match the preceding
44-target source. It remains historical evidence.

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
override `GSIM_SOURCE` and `GSIM_CXX` for another installation. The current
validation uses an independent native-WSL cache at the path shown below, at
the same pinned revision. The build does not modify that checkout and does
not require Verilator.

```sh
cd /home/openion/Valence-Net
export GSIM_SOURCE=/home/openion/.cache/valence-net/gsim-src-93b8cd23
export GSIM_CXX=clang++-19
python3 sim/run.py
# Focused development regressions:
python3 sim/run.py tcp_control tcp_managed tcp_data tcp_connection_tx
python3 sim/run.py tcp_ring tcp_endpoint tcp_reassembly tcp_congestion tcp_congested_endpoint
python3 sim/run.py ethernet_tcp ethernet_mixed ethernet_service
python3 sim/run.py tcp_header_snapshot tcp_sack tcp_multi ethernet_multi ethernet_multi_rx
python3 sim/run.py icmp_errors tcp_closed tcp_exception tcp_pmtu ethernet_errors network_errors
python3 sim/run.py tcp_pmtu_pipeline window_scale body_window tcp_generation tcp_control_priority
python3 sim/run.py memory_queue shared_receive
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
packet/checksum/byte expectations. Added adversarial tests cover ACKs overtaking
staged PMTU repairs, abort/reopen and passive-reset generations, all negotiated
window scales, clipped/overlapping receive bodies, cached-SACK transitions and
competing control events. They are not a claim of exhaustive TCP compliance or
interoperability with a physical host.

The pinned GSIM constant-concatenation, Vec-accessor and partial-state-clear
issues are documented in `sim/gsim-known-issues.txt`. Multi-context peers use
`gsim_model_init.h` to initialize the intended complete model byte range; the
integrated error test reproduces the defect with poisoned storage. This is a
simulator-only workaround and does not add RTL RAM resets or relax sanitizers.

## Vivado timing

Target part: **xczu15eg-ffvb1156-2-i**. Clock: **6.4 ns**. Setup/hold
uncertainty: **0.1 / 0.05 ns**. No false-path or multicycle exceptions are used.

The current `20261025T082252Z` run passes after local routed physical
optimization. All **222,523 routable nets** are complete; setup and hold each
cover **252,550 endpoints**, with zero failures and zero TNS/THS. Setup/hold
display **0.000/0.000 ns**, with pulse-width slack **+2.500 ns**. All twelve
coverage checks are zero, with no blackboxes or DRC/methodology/tool errors.
The uncertainties remain 0.100/0.050 ns; no timing exceptions were added.

The routed core uses **164,239 LUTs, 86,415 FFs, 109 RAMB36, 19 RAMB18 and
16 URAM**, with no DSP. Against the previous area revision this saves
**16,203 LUTs / 8.980%** and 3,946 FFs. Both setup and hold have very thin
additional margin. The fixture does not qualify SoC/board timing.
[Original reports, hashes and warning review](docs/timing/2026-10-25-082252/artifact-hashes.json)
retain the actual tool advisories.

The preceding area revision passes in **20261024T074147Z**, using local physical
refinement of the saved fully routed 061030 checkpoint. All **241,104 routable
nets** are complete; setup/hold each cover **267,119 endpoints** with zero
failures and zero TNS/THS. Setup is **0.000 ns**, hold **+0.005 ns**, pulse width
**+2.658 ns**. Coverage and routing/blackbox/DRC/methodology error checks pass.
This is OOC evidence; SoC/board timing requires a new run.

That preceding routed core uses **180,442 LUTs, 90,361 FFs, 136 RAMB36 and 20 RAMB18**,
with no URAM/DSP. Against the earlier fully routed exception/PMTU core this
saves **4,213 LUTs / 2.282%**, while adding 296 FFs and 320 LUTRAM LUTs.
Original reports, source provenance and warning review are preserved under
[that report directory](docs/timing/2026-10-24-074147/artifact-hashes.json).

The earlier exception/PMTU source closes timing in **20261024T042645Z**, a local refinement
of the fully routed **20261024T030357Z** checkpoint. Signoff remains 6.4 ns with
0.100/0.050 ns setup/hold uncertainty; the selected attempt used the same
constraints throughout. All 244,596 routable nets remain fully routed. Setup,
hold and pulse-width have zero failing endpoints; coverage, routing, blackbox,
DRC and methodology error checks are clean.

The core excluding the timing fixture uses **184,655 LUTs, 90,065 FFs,
136 RAMB36 and 20 RAMB18**, with no DSP/URAM. The positive setup margin is small;
repeat timing when integrating the SoC. [Original reports, hashes and warning review](docs/timing/2026-10-24-042645/artifact-hashes.json)
retain the actual advisories rather than describing a warning-free run.

The following figures belong to the earlier multi-context/SACK baseline.

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
# Optional memory profile (requires matching build export):
# mill -i netstack.runMain netstack.Emit build bram
# python3 fpga/prepare_registered_ooc.py --memory-profile bram
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

See the [protocol feature/limit matrix](docs/PROTOCOL-STATUS.md). The stack is
not a complete general-purpose IP stack; its verification manifest retains
`complete_protocol_stack: false`.


This revision supports 1–8 parallel TCP contexts and one local IPv4
configuration. IPv4 IHL=5 and unfragmented traffic are supported. Receive gaps
are retained in eight bounded reorder slots per connection. Cache overflow,
overlapping cached ranges and oversized ranges are dropped for sender
retransmission; the missing prefix can still enter a full cache. Unowned tuples
use the bounded reset responder; busy/reserved receive contexts are silently
dropped. There is no listening backlog or automatic context allocator beyond
the explicitly opened listener slots.

SACK recovery uses a bounded advisory scoreboard and ACK-clocked conservative
Reno recovery. It is **not a complete RFC 6675 Pipe/IsLost/rescue algorithm**.
RX SACK snapshots are built over registered steps after payload commitment;
an initial gap ACK can precede the subsequent SACK update. Further out-of-order
metadata can wait for that snapshot; this is not a sustained disorder-rate
line-rate claim. SACK does not increase retained capacity or eliminate RTO.
NewReno/ECN, DSACK, delayed ACK aggregation, timestamps negotiation, IPv6,
IP options/fragments, VLAN, multicast membership and dynamic routing remain
outside this implementation.

ICMP error handling covers the advisory types and locally generated unreachable
responses described above; Source Quench and Redirect do not drive policy.
There is no IPv4 fragmentation, forwarding/router error generation, ICMP
authentication, black-hole fallback or PLPMTUD. TCP TTL/TOS currently use internal
defaults. A secure autonomous ISN policy and configurable connection user-timeouts
are future work.

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
[ICMP](https://www.rfc-editor.org/rfc/rfc792),
[IPv4 PMTU discovery](https://www.rfc-editor.org/rfc/rfc1191).
