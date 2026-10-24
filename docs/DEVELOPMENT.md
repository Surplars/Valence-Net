# Valence-Net development evidence (2026-10-23 / 2026-10-24)

The target is 10 Gb/s Ethernet. The internal interface carries 128 bits at
156.25 MHz (20 Gb/s raw bandwidth); this is not a claim of 20G wire rate for
every packet size. Functional checks, cycle service and routed timing are
recorded separately.

Source, Git, caches and simulation stay in native WSL at
/home/openion/Valence-Net. Windows Vivado uses frozen snapshots under
E:\VM\Share\Valence-rtl. The checkout base is
0273d8dfb0219889560efdc9aa5066c4b55c69ef.

## Current exception/PMTU revision

The current working revision adds stateless TCP resets, ICMP error handling and
TCP/UDP PMTU policy. The exact-source complete regression passes all 42 targets.
build/verified.json records the 101-file source set and results; the full log is
build/icmp-pmtu-parallel-window-regression-20261024T025608Z.log. Matching routed OOC
timing passes in 20261024T042645Z: setup +0.001 ns, hold +0.006 ns. The 31-target, +0.004 ns setup /
+0.006 ns hold evidence below remains the historical multi-context/SACK
baseline; it must not be applied to changed exception/PMTU RTL.

TcpClosedResponder validates complete IPv4/TCP packets before generating a
bounded reset for an unowned tuple. ACK inputs use their ACK as the RST sequence;
other inputs receive RST|ACK covering payload plus SYN/FIN modulo 32 bits.
RST inputs and invalid/broadcast/multicast sources produce no reply. Known but
busy/reserved tuples remain silent, including CLOSED contexts with old physical
ownership and listener admission reservations. Invalid LISTEN ACKs may reset;
an unavailable listener SYN is not treated as a closed tuple. The dedicated
exception stream drops excess work instead of blocking another TCP context.

Ethernet tops identify link broadcasts before TCP/ICMP response handling.
Protocol-unreachable (ICMP 3/2) and optional UDP port-unreachable (3/3) quote the
original header plus at most eight payload bytes, excluding Ethernet padding.
UDP replies require valid length/checksum; zero UDP checksum is permitted by
IPv4. The optional elaboration-time udpPorts list selects accepted UDP ports;
the empty default keeps accept-all delivery. Error generation is bounded and
tick-rate-limited, and never responds to an incoming ICMP error.

Ipv4IcmpErrorReceive accepts supported destination-unreachable, time-exceeded
and parameter-problem codes after complete outer IP/ICMP and quoted IP
validation. Its bounded advisory queue drops excess events rather than
backpressuring because its consumer is stalled. TCP feedback checks the live
tuple, generation and sequence range of physically completed, unacknowledged
flight. An ACK-only ICMP quote at SND.NXT with no eligible unacknowledged
sequence space is intentionally rejected by this guard: an idle sender/receiver
does not learn PMTU from that quote. This narrows spoofable feedback but is not
complete ICMP authentication. Other valid advisory errors are reported without
automatically destroying the connection. ICMP quotations cannot authenticate the router.

A valid fragmentation-needed report lowers only the affected path, bounded
by the configured interface MTU and IPv4 minimum 68. A zero MTU selects a
standard lower plateau from the quoted datagram size; duplicate old quotes
cannot repeatedly lower it. TCP keeps peer MSS separate, limits data and
control-option budgets, and repairs retained data with no new sequence
allocation. Already offered headers/descriptors remain stable. Repair waits for
appropriate ACK progress, preserves cumulative-ACK ownership and normal RTO
recovery, and is marked separately so its delayed physical completion cannot
consume a later timeout retry budget. FIN/SACK repair also fits the lower MTU.
A ten-minute path expiry permits an increase only after sent and reserved
sequence space both drain.

UDP uses a four-entry destination MTU cache and an eight-entry, sixty-second
history by default. Feedback must match a successfully routed datagram's
four-tuple, IP identification/length and UDP length/checksum. A successful route
does not claim actual wire transmission. Learned entries expire after ten
minutes; flushArp clears cache and history. An oversized application datagram
is drained to its final beat, reports a local error and emits no packet.
TCP networkError and UDP udpNetworkError are Valid pulses, with no consumer
backpressure; capture them externally if persistence is needed. Local UDP
rejection takes precedence over a simultaneous advisory.

Raw-IP integration must suppress link broadcast/multicast responses itself,
provide netmask/pathMtuLimit and pass only validated icmpError events. Its
exceptionOut/exceptionDestinationIp channel is independent of tagged TCP
traffic. Ethernet stateless RST frames have txTcp=false and require no TCP
completed callback; connection-owned RST/ACK/probe/data frames retain their
normal descriptor/completion ownership. This distinction follows ownership,
not the IP protocol number.

This remains ICMP-based PMTU handling, not black-hole detection or PLPMTUD.
There is no IP fragmentation, forwarding/router role, Redirect/Source-Quench
policy, dynamic socket-port allocator or guaranteed line-rate error response.

## Multiple connections

MultiEthernetTcpUdpEndpoint now defaults to four TCP contexts, configurable
from one to eight. UDP, ARP, ICMP Echo/errors, Ethernet framing and routing are shared.
Each TCP context has independent state, generation, storage, RX window,
RTO/persist timers, Reno and SACK state. MultiTcpIpv4Endpoint exposes the same
TCP service at the IPv4 boundary; compatible single-context tops remain.

The dispatcher selects by tuple prefix, then the selected core validates the
whole packet before changing state. Passive slots may share a local port.
Conflicting active four-tuples are not consumed and assert openConflict.
Unknown tuples use the validated stateless responder; busy/reserved tuples
drop whole packets. Bounded per-context admission
prevents a blocked application or notice port from occupying common ingress
indefinitely. Packet arbitration is round-robin and locks on first offer,
including an initially stalled offer.

Every connection-owned TCP packet, including ACKs and probes, has one global
ordered tag/generation completion entry. Stateless resets use the independent
exception path without a completion entry. Each global ledger entry maps back
to its context's local entry.
Rejected, duplicate, stale or out-of-order callbacks release no ownership.
A tuple remains reserved through abort or SYN-RECEIVED-to-LISTEN rollback
until old RX work, offered frames and MAC completions drain. Even corrupt
packets finishing behind the RST cannot release it early.

Default payload storage per context is 16 KiB retained TX, 64 KiB RX and
eight separate 2 KiB reorder banks: 384 KiB across four contexts, plus
packet buffers and metadata. Each local completion ledger has 16 entries;
the default global ledger has 64. Replicated contexts increase resources
approximately with connection count; this is not a large shared-memory TOE.
At 10G, 16 KiB of TX retention covers only about 13 microseconds of flight.

## SACK and receive behavior

SACK-permitted is negotiated in SYN/SYN-ACK. A peer that does not offer it
uses the existing cumulative-ACK/Reno behavior. Receiver options report up to
four committed, live-generation payload ranges; FIN sequence space is excluded.
Adjacent ranges merge and the newest retained range is first. Duplicate-range
recency and sequence wrap are covered.

An eight-bank registered snapshot builder sorts, merges and selects blocks
over about 65 clocks. The snapshot gate applies to future payload metadata;
in-order and pure ACK metadata at the queue head do not wait for the builder.
They can still wait behind an earlier future packet in this serialized queue.
The initial gap ACK may omit SACK,
followed by an ACK when the durable snapshot publishes. There is no claim
of full-rate arbitrary reordering.

The sender remembers up to 16 advisory ranges from the four most recent
SACK-bearing ACKs. Reno's third duplicate ACK starts selective recovery;
subsequent validated duplicate/progress ACKs permit bounded additional replay.
A registered lookup skips covered data and caps retransmission at the next
hole boundary. Cumulative ACK alone advances SND.UNA and frees storage.
RTO forgets SACK state, allowing recovery from receiver reneging. Reno uses
the negotiated, bank-capped SMSS; a packet separately deducts its current
option bytes from the payload budget. Receive-gap publication therefore does
not alter congestion SMSS. A regression checks the RTO window with active
receive SACK options.
This is bounded ACK-clocked recovery, not full RFC 6675 Pipe/IsLost/rescue.

TX freezes the complete TCP data header at the retained-read handshake.
Option length, payload budget and checksum therefore stay consistent across
backpressure and replay. Tiny MSS values omit data-packet SACK when the options
would leave no payload byte; control ACKs can still report SACK.

Control ACKs capture their ACK number and durable SACK ranges before a
separate encoding clock. Count-zero controls retain their normal cadence.
controlPending includes this work even before control.valid is exposed, so
ownership cannot drain while a control header is being encoded.

Data-option validation/packing is also registered. The cached encoding is used
only with matching ACK base, generation and published block count; otherwise
the packet omits SACK until a compatible copy is available. ACK and receive
window stay live together. The complete selected header and matching payload
budget are then frozen at the original retained-read handshake.

Read-to-empty window updates wait for 32 clocks without another application
read, coalescing tiny-packet bursts. Batched credit and reads from a completely
full ring request an immediate update. A final short read still gets an update.
This removed a second ACK per received byte that overloaded the complete MAC
path in single-context service tests.

## Functional evidence

The current complete regression passes all 42 targets with ASan/UBSan.
build/verified.json records the exact 101-file source set, model/object hashes
and every result; those source hashes match the working checkout. The run log
is build/icmp-pmtu-parallel-window-regression-20261024T025608Z.log. This includes all six
new exception/PMTU targets and the existing full-Ethernet service checks.
Its physical_timing_verified field identifies simulation-only evidence; the
separate source-matched routed qualification below supplies physical timing.

Five additional timing-regression targets also pass:

- tcp_pmtu_pipeline: partial/full ACK overtaking a staged repair, abort/reopen,
  passive reset epochs and RTO priority.
- window_scale: all sixteen requested scale encodings, 448 credit cases,
  passive rollback, fallback renegotiation and zero-window reopening.
- body_window: 1,152 clipped/overlapping receive bodies, FIN right edges,
  durable versus failed storage and sequence wrap.
- tcp_generation: the original SACK generation guard checked every cycle across
  200 cache/epoch gaps; a passive-reset mutation is rejected independently.
- tcp_control_priority: 96 simultaneous-open cases and 512 competing events
  across ESTABLISHED/CLOSE-WAIT, completion and pending window updates.

The cold PMTU offer is registered before shared replay admission; an ACK that
retires a sampled offer causes a retry at the live oldest sequence. One-hot
negotiated scaling and guarded receive-room arithmetic shorten the window
path. SACK generation changes disable encoding until the cache catches up;
live ACK/count checks remain. Replay bounds use a parallel four-way minimum,
and state-proven OPEN/close/SYN conditions keep unrelated validation out of
payload admission and receive-body arithmetic. Normal packet cadence is unchanged.


The historical baseline completed 31 targets, including registered OPEN,
completion delivery, checksum seed and retained-read validation. Its exact
80-file source set matches build/vivado/20261023T113414Z, which retains
subsequent-full-regression.json without rewriting the original focused-test
provenance. That timing pass belongs only to the historical source.
Clang 19.1.1 runs independent C++ peers with ASan/UBSan. Tool versions are
Mill 1.1.7, Scala 2.13.17, Chisel 7.3.0 and pinned GSIM
93b8cd23edd3228807c4f2a08c19c3936a463cb2.

Six additional targets cover the exception/PMTU revision:

- tcp_closed: all 256 flag combinations, modular reset sequence arithmetic,
  malformed IPv4/TCP checksums/options/lengths, overflow recovery, rate limiting,
  local broadcast suppression and output stability under stalls.
- tcp_exception: listener/unknown tuple behavior, no reset for busy or reserved
  ownership, and shared-ingress progress with the exception output blocked.
- icmp_errors: supported ICMP error parsing, quote checksum/length/address
  rejection, exact short/odd unreachable quotes, UDP checksum checks, bounded
  queues and rate limiting.
- tcp_pmtu: new-segment limits, retained partial repairs, sequence wrap,
  repeated decreases during a stalled offer, stale generations, option budgets
  at MTU 68, FIN repair, and timeout-versus-PMTU completion attribution.
- network_errors: live TCP quote fencing, soft errors, idempotent zero-MTU
  plateaus, idle-only path expiry, UDP route-success history, cache/history
  expiry, replacement, flush and local oversize draining.
- ethernet_errors: complete Ethernet unknown-TCP resets without ledger
  ownership, unreachable replies/UDP binding, link-broadcast suppression,
  forged ICMP rejection, wrapped TCP 1500-to-600 repairs and UDP PMTU rejection
  with context/destination isolation.

The independent integration probe
build/ethernet-errors-probe-20261023T142321Z passed ASan/UBSan with independent
wire checksums. Its verified.json explicitly identifies a source snapshot
before the final FIN/SACK PMTU changes. The current full42 run now supersedes
that probe as same-source functional evidence; neither is a timing result.

This test also reproduces a pinned GSIM initialization defect: generated
memset uses a uint32_t pointer difference as a byte count, clearing only about
one quarter of the intended state. Poisoned host storage exposes an impossible
wide index from a narrow RTL FIFO RAM. sim/gsim_model_init.h clears the complete
generated primitive-state byte range before init/reset, preserving control
fields and derived C++ members. It does not patch generated code, change RTL
RAM reset behavior or relax sanitizers. Details and earlier tool issues remain
in sim/gsim-known-issues.txt.

The historical multi-context/SACK additions include:

- tcp_header_snapshot: reserve/replay handshake changes between zero and
  36 option bytes, MTU, checksums and retained bytes. A deliberate mutation
  restoring live headers fails with the expected MTU-overflow error.
- tcp_sack: negotiation/fallback, latest-first/adjacent blocks, duplicate
  recency, FIN/wrap, multiple and unaligned holes, malicious blocks,
  retained ownership, reneging/RTO, generations and tiny MSS (including one).
- tcp_multi: independent windows/storage/RTO, same-port listeners,
  simultaneous SACK gaps, conflicting opens, fairness, abort and cross-slot
  reuse, global completions and SYN-ACK/RST ownership. An old model fails
  the corrupt-packet-behind-RST regression.
- ethernet_multi: four contexts sharing ARP/UDP/ICMP, stalled-context
  isolation, complete wire checksums and sustained TX with peer ACKs.
- ethernet_multi_rx: strict 10G input deadlines, all offered bytes checked,
  single/four-context cases, zero-window reopening after one read beat,
  and the final coalesced read-to-empty update.

The permanent configuration_probe.py independently emits full default-storage
tops with 1, 3 and 8 contexts. The current probe
build/configuration-probe-20261024T030331Z passes all three with zero warnings
and matching authoritative Scala hashes. Its provenance records elaboration,
not functional simulation or physical timing for each context count.

Existing packet, checksum, storage, ring, timer, connection, reassembly,
congestion and mixed-traffic tests remain part of the full regression.
Selected checks include 6,400 checksum packets/all 65,536 keep masks,
2,788,617 RX-ring bytes, 4,801 retained-store replays and all eleven TCP states.

## Scheduling evidence

The current 42-target run repeats the complete-Ethernet service cases.
Four-context TCP TX retains 9/100-clock service for 1/1460-byte payloads, and
single/four-context RX meets every strict 10G offered-load deadline with zero
drops. These are measured cycle budgets at the intended clock, not a current
physical clock-closure result.

The multi-context functional harness has four TX banks and a 2 KiB RX
ring per context, with reduced ARP/completion/timer settings. This deliberately
differs from production storage. Physical callbacks and peer ACKs are modeled,
and tests use cached ARP and consumers that keep up. The single-active large
RX case selects that application's read port continuously; a test-only
round-robin read mux would otherwise limit its drain rate to four bytes/clock.

| Complete Ethernet path | Conditions | Observed service |
| --- | --- | --- |
| TCP TX, 1 byte | Four contexts, 256 frames | 9 clocks/frame, 11.667 Gb/s wire-equivalent |
| TCP TX, 1460 bytes | Four contexts, 256 frames | 100 clocks/frame, 19.225 Gb/s wire-equivalent |
| Pure ACK RX | One/four active, 256 frames | 10.5 clocks/frame offered, zero drops |
| TCP RX, 1 byte | One/four active, 4096 frames | 10.5 clocks/frame offered, zero drops |
| TCP RX, 1460 bytes | One/four active, 256 frames | 192.25 clocks/frame offered, zero drops |

TX excludes startup/drain and measures 255 completion intervals, with all
retained banks cumulatively ACKed at the end. RX deadlines include all MAC
input handshakes and are not stretched when the DUT stalls. Wire conversion
includes Ethernet header/padding/FCS, preamble/SFD and IFG. Raw-IP burst
measurements are not substituted for sustained Ethernet service.

The compatible single-session ethernet_service target also passes. UDP TX
uses 10 clocks for 0/1/17-byte payloads (10.500 Gb/s wire-equivalent), 41 for
512 bytes (17.622 Gb/s), and 97 for 1400 bytes (18.892 Gb/s). UDP RX remains
7/38/94 clocks for those respective sizes. The PMTU sent-history route fence
adds one TX clock per datagram versus the old baseline. TCP TX retains
9/13/100 clocks for 1/64/1460 bytes, and 1-byte TCP RX retains 9 clocks.
The table in README records the corresponding capacities and conditions.

A current-source production-capacity probe at
build/production-rx-probe-20261024T030328Z checks the compatible single-session
Ethernet top with a 64 KiB RX ring, eight TX banks and reduced ARP/completion/
timer settings. TCP RX payloads 1/64/512/896/1460 bytes measure
9/11/39/63/98 clocks per frame; the 1460-byte case is 19.617 Gb/s
wire-equivalent scheduling capacity. Its authoritative source hashes match
the current 42-target full-regression snapshot. The earlier 114045Z probe is
historical only. The new probe does not replace the four-context
strict-offered-load test or board measurements.

Its companion reassembly probe reuses the same verified model for 24 waves
of eight retained 1460-byte gaps and a missing prefix: 315,360 ordered bytes
across 131,024 cycles. Sequence/address wrap, shuffled arrivals, consumer
stalls and independent checksums pass. This checks capacity and ordering,
not sustained disorder-rate throughput.

Earlier source results remain recorded in history/development-2026-10-22.md
and verification-2026-10-22.json.

## Physical timing

The current full-default four-context source passes routed OOC timing in
build/vivado/20261024T042645Z on xczu15eg-ffvb1156-2-i. Required clock/uncertainty
remain 6.4 / 0.100 setup / 0.050 hold ns. Setup is +0.001 ns, hold +0.006 ns,
and pulse-width slack +2.658 ns. Setup/hold each cover 265,035 endpoints with
zero failing endpoints and zero TNS/THS. All twelve coverage checks are zero;
all 244,596 routable nets are fully routed, with zero routing, DRC/methodology
errors and blackboxes. SoC/board timing is not included, and the small positive
setup margin requires renewed closure after integration.

The initial source-matched 030357 route ended at -0.007 ns setup / +0.006 ns
hold with five setup violations. fpga/prepare_routed_refinement.py freezes its
complete routed checkpoint, RTL provenance and reference-DCP SHA. The local
fpga/refine_routed.tcl flow accepts only complete routing and zero timing
failures, then emits all final reports and a DCP. The winning first attempt
used the original 0.100 ns setup uncertainty; the optional stricter targets
were not used. Avoiding an unnecessary global reroute preserved this complete
routed result. No frequency, signoff uncertainty or data-path exception was
relaxed, and protocol source hashes remained identical to full42.

Core resources excluding the fixture are 184,655 LUTs, 90,065 FFs, 136 RAMB36
and 20 RAMB18 (no DSP/URAM). Published originals, source qualification, hashes
and warning review are under timing/2026-10-24-042645. Four Vivado 12-4439
messages concern large register-report collections; both complete register
reports and final DCP completed. RTSTAT-10 concerns only the fixture signature
output; SYNTH-6/16 are retained BRAM advisories and CLKC-3 the fixed clock site.
These advisories are reviewed, not erased. The following history describes
earlier sources.

The historical full-default four-context snapshot is
build/vivado/20261023T113414Z. It completed physical refinement and routing
with setup +0.004 ns, hold +0.006 ns and pulse-width slack +2.658 ns. Setup and
hold each cover 241,228 endpoints, with zero failing endpoints and zero TNS/THS.
All twelve coverage checks are zero, including unconstrained endpoints, missing
clocks/delays and combinational loops. All 223,179 routable nets are fully
routed, with zero routing/DRC/methodology errors and zero blackboxes.

The core uses 172,027 LUTs, 81,490 FFs, 132 RAMB36 and 18 RAMB18. Including the
retained timing fixture gives 172,861 LUTs and 87,462 FFs. The setup margin is
4 ps for this exact fixture/constraint set; integrated SoC and board timing
need their own checks.

Main-checkout SACK/multi-context/full-Ethernet focused regressions pass,
and configurations 1/3/8 elaborate without warnings.
A fresh source/RTL comparison passed before freezing. All 31 regression targets pass against this frozen source, preserved as
subsequent-full-regression.json, with unchanged 9/100-clock Ethernet TX and
strict 10G RX.

This snapshot registers OPEN priority availability while retaining current
core.ready on the actual handshake. It also registers reassembly scheduling
credit at the serialized receive boundary; live ACK/window outputs remain
unchanged. Five fixed SACK option payload budgets are computed in parallel,
then selected with the option length, removing late subtraction/clipping
from the path to RAM range validation.

Ten earlier executed multi-context snapshots remain diagnostic only:

| Snapshot | Synthesized estimate | Longest synthesized path | Status |
| --- | ---: | --- | --- |
| 20261023T011723Z | -5.914 ns | 42 levels, OPEN / SACK option budget / Reno cwnd | Stopped after synthesis |
| 20261023T015206Z | -6.119 ns | 44 levels, tuple admission / OPEN / immediate SACK clear / payload read | Stopped before route |
| 20261023T021949Z | -3.487 ns | 36 levels, OPEN / SACK replay clear / payload lookup diagnostic | Stopped before route |
| 20261023T024735Z | -3.131 ns | 38 levels, ACK validation / SACK control-option encoding | Stopped after placement; physopt estimate -3.236 ns |
| 20261023T034015Z | -1.641 ns | 26 levels, cross-context OPEN priority | Stopped during route; physopt -1.935 ns, route intermediate -2.945 ns |
| 20261023T044217Z | -1.461 ns | 25 LUT levels, tuple configuration / OPEN / SYN option packing | Superseded during global placement by verified control pipeline |
| 20261023T050746Z | -1.161 ns | 26 levels, SACK compatibility / payload read range validation | Placed -2.508 ns, physopt -2.054 ns, route intermediate -1.789 ns; stopped for status/ready-path pipelines |
| 20261023T055720Z | -0.706 ns | 25 levels, PUSH / TCP checksum seed | Completed route and refinement: setup -0.912 ns, hold 0.000 ns; failed setup |
| 20261023T071737Z | -0.459 ns | 22 levels, IPv4 buffer to slice | Completed route/refinement: setup -0.189 ns, hold +0.002 ns; failed setup |
| 20261023T103352Z | -0.755 ns | 25 levels, local scale to control window | Placed -0.096 ns, physopt -0.076 ns, route intermediate -0.562 ns; stopped for exact idle-state guard simplification |

Active OPEN now captures configuration before emitting its SYN one clock
later. Pending SYN work participates in controlPending/idle ownership. A new
regression changes all OPEN inputs immediately after handshake and requests
abort before control.valid; the committed SYN remains intact and cannot be
bypassed. RX end/window arithmetic uses 32-bit modular carry-save compression and
parallel FIN predicates; timer comparisons occur before selecting candidate
sequence words. Independently and jointly verified candidates preserve
normal TX/RX service cadence.

OPEN eligibility is now compared in parallel without serial grant propagation.
Reno uses negotiated bank-capped SMSS rather than live packet option budgets.
SACK output clearing now takes effect at the clock edge, like its published
state. Parent control/data handshakes already cancel on the clear cycle;
removing immediate combinational count clearing cuts the OPEN-to-payload
arithmetic dependency without adding packet/header latency.

The remaining redundant OPEN terms are removed: classifying a packet
already excludes OPEN, and OPEN can fire only in CLOSED (already inactive).
These equivalent state guards no longer feed dispatcher/OPEN combinational
logic into SACK replay clearing.

Freeze 20261023T053420Z passed functional verification but was never launched
in Vivado; it was superseded before execution. It provides no physical timing
evidence. That baseline source includes the registered OPEN grant, pipelined
payload-read validation, per-context two-beat TX elastic queue with complete
wire descriptor, and registered RX/per-context diagnostics. These address
the actual placed status path, read-range path and cross-context ready path.

The completed 055720 baseline route failed at -1.231 ns setup and +0.006 ns
hold; post-route refinement finished at -0.912 ns setup and 0.000 ns hold.
All 219,381 routable nets were routed, with zero routing/DRC errors and no
unconstrained endpoints. Its core used 172,873 LUTs, 78,817 FFs, 132 RAMB36
and 18 RAMB18. These are prior-source measurements, not a current timing pass.
The final longest path ran from registered OPEN request into receive-store
state control. The subsequent baseline snapshot additionally registers successful sequence
completion delivery and defers OPEN receive clear within the drained multi
wrapper. An exact 17-bit PUSH tail predicate and command-captured TCP header
derivatives/checksum seed address the other measured baseline critical paths.
Snapshot 071737 estimates -0.459 ns after synthesis, -0.094 ns after placement
and -0.077 ns after physical optimization. Its first complete route failed at
-0.401 ns setup / +0.005 ns hold. Post-route refinement completed at -0.189 ns
setup / +0.002 ns hold, with 781 failing setup endpoints and TNS -37.097 ns.
The default core used 171,268 LUTs, 81,460 FFs, 132 RAMB36 and 18 RAMB18.
Routing and DRC checks were clean. Its final critical groups are retained-store
diagnostics, receive ACK/window arithmetic and credit-to-reserved arithmetic.
Snapshot 103352 applies the routed-path fixes: retained-TX diagnostics gain a
child register before parent OR aggregation; receive offset/room/end candidates
use carry-save arithmetic with the simultaneous-SYN increment kept separate;
a 16+16 carry-select update shortens late reservation-to-sequence arithmetic.
PacketSlice evaluates its byte mask and data shift in parallel. Control option
bytes above optionLength retain their registers rather than clearing on every
emit; SYN rewrites its low 96 bits and staged SACK encoding rewrites all option
bytes. Checksum/header masking still excludes unused bytes. These changes pass
all 31 tests and preserve measured Ethernet service cadence. Full-default
four-context snapshot 103352 was stopped during routing after an intermediate
-0.562 ns. Its interrupted-status.json and partial-results directory explicitly
retain non-final reports/checkpoints; they are not a completed route result.

Snapshot 113414 removes a measured admission-to-metadata-ready dependency.
While the parent receive queue is idle, RBO enqueue/touch signals are false
because they require its decision state. A separate writer/snapshot-state busy
output is therefore exactly sufficient for the idle metadata guard. The full
RBO busy output remains unchanged for low-level consumers. All seven focused
test result strings, including cycle counts, match 103352 exactly. The new
full31 regression passes with every result string unchanged. Placement and
physical optimization both report +0.106 ns setup. The first complete route
reports -0.260 ns setup / +0.006 ns hold. AggressiveExplore physical optimization
and Explore rerouting complete at +0.004 ns setup / +0.006 ns hold. The source
then selected for main was byte-identical to the pushed a2733af code and this passing
physical snapshot. A fresh restored-source emission reproduces all 77 core
RTL files byte-for-byte. Further arithmetic candidates remain in an isolated
local checkpoint and are not included in the timing claim.

Published original summary/route/utilization/DRC/methodology reports and their
hashes are under timing/2026-10-23-113414. The warning review records the four
Vivado 12-4439 report-collection-size messages; every requested register report
and final DCP completed successfully. DRC RTSTAT-10 concerns only the fixture
signature output. Methodology records inferred-BRAM output-register/collision
advisories and the intentionally fixed BUFG site; these are retained with the
reports, not described as a warning-free run. No tool/native errors occurred.

The timing flow preserves a failed first route as baseline_route.dcp/reports
and attempts one AggressiveExplore physical optimization plus Explore route.
It adds no timing exceptions and keeps all final setup/hold/coverage checks.

These synthesis estimates are not routed timing passes.

Vivado 2025.2 targets xczu15eg-ffvb1156-2-i with a 6.4 ns clock and
0.1/0.05 ns setup/hold uncertainty. ValenceNetTimingTop retains real input/output
registers and a located BUFG around the unchanged default-storage core.
No false-path or multicycle exceptions are used. The fixture is a static
timing load, not a MAC or board application.

Earlier single-context evidence is historical:

| Snapshot | Scope | Setup WNS | Hold WHS |
| --- | --- | ---: | ---: |
| 20261022T150138Z | Previous non-SACK core with reassembly | +0.001 ns | +0.003 ns |
| 20261022T154810Z | Checksum compressor probe only | -0.003 ns | +0.007 ns |

The failed compressor probe moved the critical path to retained-read
coordinates. Subsequent changes register RX free credit directly and remove
redundant range-validation dependencies from coordinate/keep register enables;
invalid reads still return errors and produce no payload. The checksum helper
uses balanced carry-save one's-complement compression. SACK range sorting and
recovery lookup also use registered stages.

A separate current-source single-session check at
`build/single-closed-response-probe-20261024T030519Z` reuses the verified 64 KiB model with
`closedResponses=true`. Unknown tuples return correct untagged RSTs;
incoming RSTs and invalid checksums are suppressed. During stalled exception
output, valid RX continues and subsequent real TCP route/completion counts,
tags and generations remain correct. Active connections and wildcard listeners
are covered under ASan/UBSan; this is a correctness probe, not a throughput test.

## Reproduce and scope

~~~sh
cd /home/openion/Valence-Net
export GSIM_SOURCE=/home/openion/.cache/valence-net/gsim-src-93b8cd23
export GSIM_CXX=clang++-19
python3 sim/run.py
# Optional focused exception/PMTU run (not a substitute for the full run):
python3 sim/run.py icmp_errors tcp_closed tcp_exception tcp_pmtu ethernet_errors network_errors
python3 bench/configuration_probe.py
python3 bench/production_rx_probe.py
python3 bench/production_reassembly_probe.py PRODUCTION_PROBE_DIRECTORY
python3 bench/single_closed_response_probe.py PRODUCTION_PROBE_DIRECTORY
python3 fpga/prepare_registered_ooc.py
~~~

For source-matched local refinement after a completed, fully routed run:

~~~sh
python3 fpga/prepare_routed_refinement.py build/vivado/REFERENCE_RUN
~~~

Run/collect this new snapshot with the same Windows wrapper. The auditor also
verifies the reference checkpoint hash; it never accepts a partially routed
physical-optimization result.

Use fpga/run_windows.ps1 with -Mode Run -Phase route, then -Mode Collect;
audit collected reports with fpga/summarize_ooc.py. The preparer verifies
authoritative hashes and byte-compares a fresh RTL emission before freezing.
Generated simulation objects can be reused only after fresh GSIM generation
and exact model/header/compiler/flag and object-hash checks.

The routed audit requires zero failing setup/hold/pulse-width endpoints and
zero total negative slack, nonnegative register paths, a final TIMING_MET
result, complete routing/coverage and no DRC/methodology/tool errors. A rounded
0.000 ns WNS alone cannot pass. The auditor was checked against the known-good
113414 reports and four deliberate negative fixtures; performance/constraint
critical warnings remain an explicit review item. These are auditor checks,
not new hardware timing results.


Full RFC 6675 recovery, DSACK, ECN, timestamps, black-hole detection/PLPMTUD,
configurable user timeouts and autonomous secure ISNs remain future protocol
work. IPv6, IP fragments/options and VLAN are absent. Stateless reset and ICMP
response paths are bounded, and advisory notices are not guaranteed delivery.
Sustained loss/reordering/long RTTs need additional evidence.
MAC/PCS/PHY, CDC, DMA/CSR/Linux integration and real-peer board testing remain
outside this revision. Repeat timing after integration.
