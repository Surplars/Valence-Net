# Valence-Net — hardware TCP/UDP stack

Independent Chisel project for the Valence FPGA SoC. The target is an autonomous
1G/10G Ethernet TCP/UDP stack; Linux drivers and application interfaces follow
the protocol implementation. This directory is separate from the active SoC
worktrees. Development and simulation run in Ubuntu 24.04 under WSL.
The project lives at /home/openion/hardware-netstack on the native Linux
filesystem. Keep source, build outputs and compilation caches there; do not
move the working tree to /mnt/c.

## Current implementation

* `InternetChecksum`: 128-bit streaming Internet checksum, seeded pseudo-header
  sums, odd byte boundaries, invalid keep detection and output backpressure.
* `UdpValidate`: IPv4 UDP header, length, pseudo-header and checksum validation.
  A zero checksum field is accepted as IPv4 checksum omission. Wire value 0xffff
  is checked normally. Errors are held until the verdict is accepted.
* `PacketCommitBuffer`: two independently owned packet banks using synchronous
  RAM, delayed commit/drop, elastic output, simultaneous receive/transmit,
  oversize draining and reset cancellation. Context follows committed packets.
* `UdpReceive`: atomic fork to checksum validation and packet storage. Only
  complete, accepted datagrams can be read. Outputs retain the UDP header and
  carry the source/destination IPv4 addresses.
* `PacketSlice`: variable prefix removal, exact payload extraction and trailing
  padding removal. Handles arbitrary byte alignment, empty payloads, short input
  recovery and late stream errors under random backpressure.
* `Ipv4Validate` / `Ipv4Receive`: verify the entire IP packet before release,
  check header checksum/length/version/destination, and remove the IP header and
  link padding. Local IP and limited-broadcast permission are captured per packet.
* `Ipv4UdpReceive`: complete IP-datagram-to-UDP-payload chain, with source and
  destination addresses/ports and payload length alongside each output beat.
  Non-UDP protocols are drained separately for now; TCP/ICMP dispatch follows.
* `Ipv4UdpTransmit` / `PacketPrepend`: validate and retain an outgoing payload,
  generate IPv4 and UDP headers/checksums, then emit a packed IP datagram. Reject
  inconsistent lengths, stream errors, TTL-zero send commands and buffer overflow.
  Generates DF=1 and converts a computed zero UDP checksum to wire value 0xffff.
* `ArpFrameReceive` / `ArpFrameTransmit`: decode Ethernet/IPv4 ARP after the full
  frame (including any late MAC error), and serialize 60-byte Ethernet-II ARP
  frames with zero padding. Preamble and FCS belong to the MAC boundary.
* `ArpEngine` / `ArpEndpoint`: next-hop resolution, local-address replies,
  associative neighbor cache, refresh/expiry, round-robin replacement, bounded
  retries, explicit cache flush and address-conflict notification. Resolution
  results and frames retain their values under backpressure. Request timers
  start only after the transmitted frame's final beat is accepted.
* `EthernetDispatch` / `EthernetIpv4Receive`: frame-level ARP/IPv4 classification,
  local/broadcast MAC filtering, complete-frame validation and Ethernet header
  removal. Late MAC/FCS errors cannot expose an IPv4 payload.
* `EthernetIpv4Transmit` / `EthernetPad` / `PacketArbiter`: connected-subnet and
  gateway routing, MTU checks, retention during next-hop resolution, Ethernet
  encapsulation and zero padding, and noninterleaving round-robin transmission.
  The arbiter locks on an offered first beat even when the MAC is stalled.
* `EthernetUdpEndpoint`: integrated bidirectional Ethernet/ARP/IPv4/UDP endpoint.
  Failed ARP resolution drains the retained IP packet with a drop notification;
  successful cold resolution transmits the original datagram without losing it.
* `TcpValidate` / `TcpReceive`: whole-segment TCP checksum/length validation,
  option parsing and committed, header-stripped payload delivery. Parses MSS,
  window scale (clamped to 14), timestamps, SACK permission and up to four SACK
  blocks. Unknown well-formed options are skipped; malformed encodings drop the
  packet. SYN-only options are ignored outside SYN. Parsing is not negotiation,
  sequence acceptance or SACK recovery; those belong to the connection engine.
* `Ipv4TcpTransmit` / `VariablePacketPrepend`: retain/validate payload, generate
  both IPv4/TCP checksums, and serialize a 20–60-byte TCP header plus payload.
  The internal command supplies padded option bytes; unused option bytes are
  masked. Reject invalid option length, payload length, stream errors, TTL-zero
  commands and overflow. A computed zero TCP checksum is transmitted as zero.
* `TcpRetransmissionTimer`: per-connection RTT estimation, Karn sample exclusion,
  bounded exponential RTO backoff, sequence-wrap-aware ACK accounting and SYN
  timeout fallback. A sticky expiration waits for ACK progress or a completed
  retransmission; queuing a packet alone never starts/restarts this timer.
* `TcpPayloadStore`: synchronous-RAM banks retain payload until explicit release.
  Repeated arbitrary byte-range reads support partial-ACK retransmission.
  Generation-tagged tokens reject stale descriptors after bank reuse. Releasing
  a bank during replay defers reuse until the current output finishes.

* `TcpConnectionControl`: per-TCB active/passive/simultaneous open, all eleven
  connection states, queued-versus-wire sequence accounting, bounded ACK
  validation, receive trimming, MSS/WS negotiation, FIN/half-close, challenge
  ACK throttling and TIME-WAIT. Only payload already retained by the caller
  can advance the receive sequence; the disposition owns its storage token.
* `TcpTransmitQueue`: connects retained payload banks to ordered sequence
  reservations, MSS/available-credit segmentation, cumulative-ACK reclamation
  and arbitrary partial-ACK replay. A read pins its bank before publishing a
  segment. ACKs can retire that bank during replay without overwriting its data.
* `TcpDataTransmit`: connects that scheduler to `Ipv4TcpTransmit`, producing
  complete checksummed IPv4/TCP data packets with generation/context metadata.
  Packet preparation overlaps previous packet output; queuing a packet does
  not report physical transmission.
* `TcpManagedConnection`: connects that TCB to the RTO estimator/timer. SYN and
  FIN timeouts generate control retransmissions; data timeouts request replay
  from the oldest unacknowledged sequence. Only completed retransmissions count
  toward the retry limit (eight by default). Exhaustion aborts and reports the
  connection generation. ACK-event backpressure does not delay timer updates.

Input data byte 0 is bits 7:0. The lower-level `UdpReceive` input starts at the UDP header, with full
16-byte nonfinal beats and a contiguous nonzero final keep. IPv4 metadata is
sampled with the first accepted beat. The current receive fast path requires
UDP length to equal IPv4 payload length; unsupported/malformed input is dropped.
The configurable default buffer holds 1,024 beats per bank (16 KiB of packet
data); memory use also includes keep/last/error fields. Test harnesses deliberately
use 16 beats per bank to exercise capacity boundaries and wrap frequently.

`Ipv4UdpReceive` starts at the IPv4 header and returns only application payload.
`Ipv4UdpTransmit` accepts one command followed by one payload and emits a complete
IPv4 datagram. Zero payload length is represented by one keep=0,last=true beat.
These are internal hardware contracts, not a Linux driver ABI.

This is **not yet a complete TCP/UDP stack**. Ethernet IPv4/ARP/UDP paths are
integrated. The TCP connection state machine and RTO timer are connected, but
the data sender now connects retention/segmentation/replay to IPv4/TCP packet
serialization. These paths, receive ownership, the session dispatcher and
congestion control are not yet assembled into an operational TCP endpoint. ICMP is also missing.
The current IPv4 fast
path accepts IHL=5 and unfragmented packets, rejecting options and fragments.
Destination filtering covers one local unicast address and optional limited
broadcast. Reassembly, multicast membership and IP option semantics are not
implemented. Received TTL 0/1 is accepted at the destination host per RFC 1122
3.2.1.7; a transmitted TTL of zero is prohibited.

ARP resolves a supplied next-hop IP; gateway selection remains a separate routing
function. Production defaults are 16 cache entries, a 1-second retry interval,
three attempts and a 60-second cache lifetime, using a 1-ms `tick` enable. Values
are elaboration parameters; the test harness uses 4 entries and shorter timers.
Flush cancels pending resolution and queued transmissions, returning an error
for a canceled lookup, while an already started frame is allowed to finish.
Change local MAC/IP configuration together with flush. ARP address probes with
sender IP zero receive replies but never create cache entries. Existing entries
can refresh from gratuitous ARP; unsolicited unrelated replies cannot resolve
another pending lookup. Address-conflict notification is not a full RFC 5227
address-selection/defense implementation.

The integrated endpoint routes via one configured connected subnet plus an
optional on-link gateway, and enforces the configured IP MTU on transmit.
Limited/directed broadcasts and IPv4 multicast map directly to their Ethernet
addresses on transmit. Receive filtering currently accepts local unicast and
limited broadcast only; directed-broadcast/multicast reception, VLANs and local
self-loopback are not implemented. Configure MAC/IP/netmask/gateway/MTU while
the endpoint is quiescent; runtime ARP cache flush can cancel an outstanding
lookup. MAC-facing streams exclude preamble/FCS/IFG. A physical MAC adapter must
buffer/drop whole incoming frames when downstream is blocked; Ethernet wires
cannot honor the internal ready signal.

## Run in WSL

```sh
cd /home/openion/hardware-netstack
python3 sim/run.py
# Run only affected components during development:
python3 sim/run.py slice ip_receive transmit
python3 sim/run.py arp
python3 sim/run.py ethernet
python3 sim/run.py tcp_receive tcp_transmit
python3 sim/run.py tcp_timer tcp_store
python3 sim/run.py tcp_control tcp_managed tcp_data
```

Dependencies: Mill 1.1.7, Scala 2.13.17, Chisel 7.3.0, Clang 19+, and GSIM
revision `93b8cd23edd3228807c4f2a08c19c3936a463cb2`. The existing GSIM checkout is
read from `/home/openion/Valence/simulator/build/gsim-src`; override `GSIM_SOURCE`
and `GSIM_CXX` for another installation. The build does not modify that checkout.
There is no Verilator dependency.

The script compiles Chisel, emits FIRRTL and SystemVerilog, builds GSIM models,
and runs independent C++ packet/byte oracles with ASan and UBSan. Logs and the
source-hashed result manifest are under `build/`. A failed run removes the old
`build/verified.json`. A focused run records only its selected tests in
`tests_run`/`results`; old log files are not promoted into current test evidence.
Source hashes are captured before compilation and checked again after tests;
changing source during verification prevents a manifest from being written.
Generated files are not source files.

Default-depth standalone RTL is in `build/rtl/` (`UdpReceive`), `build/ipv4-rtl/`
(`Ipv4UdpReceive`), `build/ipv4-tx-rtl/` (`Ipv4UdpTransmit`), and `build/arp-rtl/`
(`ArpEndpoint`). `build/ethernet-rtl/` contains the integrated default-depth
`EthernetUdpEndpoint`. Harness RTL uses
separate directories where geometries differ, to avoid shared module-name
collisions. Functional randomized simulation presently covers reduced-depth
buffers (16, 32, 64 or 128 beats); default-depth export alone is not a proof of its FPGA
timing or resource use. Do not combine the separate RTL directories into one
compilation library; each includes its own dependency modules.

Standalone TCP wire RTL is in `build/tcp-receive-rtl/` and
`build/tcp-transmit-rtl/`. TCP tests use 32-beat banks. Receive verification
checks 2,400 segments (1,000 delivered, 1,400 rejected), including malformed
options, reserved bits, all SACK block counts and a valid zero TCP checksum.
Transmit verification checks 2,600 commands (1,400 emitted, 1,200 rejected)
against independent complete IPv4/TCP byte sequences, all eleven legal header
lengths, unused option masking, backpressure, buffer overflow and mid-packet
reset. TCP control-only packets still supply a keep=0,last payload beat on the
internal interface. The receive consumer must drain every accepted metadata
event's payload, even when its connection/sequence number is rejected.

The standalone timer uses a 1-ms tick, a 1-second initial/minimum RTO and a
60-second maximum. SRTT/RTTVAR retain eight fractional bits; each timer arm
includes one extra tick to cover tick-phase uncertainty. The test uses a
100-ms tick with the same limits in seconds and an independent absolute-deadline
model, covering 84,069 cycles, 1,027 RTT samples, 134 expirations and 410 Karn
exclusions. Sequence acceptance here only protects timer accounting: the session
engine must still validate the ACK segment and decide which bytes to release.
Default RTL is in `build/tcp-timer-rtl/`.

The retained payload store defaults to eight 2-KiB banks (16 KiB of payload RAM)
and permits one writer plus one replay reader concurrently. A successful
`stored` handshake transfers ownership; reads/releases before that handshake
are rejected. A failed read returns an error response without a payload. A
successful zero-length read emits one keep=0,last beat. Reset invalidates all
tokens and must reset the connection engine's corresponding descriptors too.
Default RTL is in `build/tcp-store-rtl/`; the test uses four 256-byte banks to
stress exhaustion and frequent bank reuse. Its independent byte scoreboard
checks 1,409 writes (200 rejected), 4,801 replays, 1,005 rejected reads, 1,374
cycles with simultaneous read/write transfers, and ten resets. Cases include
release during stalled replay, full-bank allocation pressure, ownership-transfer
backpressure, stale-token rejection and arbitrary offsets. This is payload retention, not an
ACK policy or a completed retransmission engine.

The connection-control tests use independently generated peer events after TCP
wire validation, rather than pretending to be whole-wire interoperability tests.
They cover 8,086 segment events, 1,286 sequence reservations and all eleven TCP
states over 67,642 cycles, including 240 sequential randomized connections.
Coverage includes simultaneous open, data/FIN on SYN, overlap clipping, failed
payload storage, zero windows, half-space ACK rejection, MSS/WS limits, stale
window updates, challenge ACKs, old-generation completions, both half-close
directions, crossed FINs and TIME-WAIT FIN replay. Each output channel receives
independent backpressure and its held fields are checked for stability.

The managed-connection test adds automatic SYN/SYN-ACK/FIN retries, data replay
requests after partial ACKs, blocked physical transmission, stalled ACK-event
consumers, ACK/timeout coincidence, retry exhaustion and reuse after timeout.
Its focused scenario covers 1,059 cycles and 12 received segment events. The
test uses ten ticks/second and a shortened TIME-WAIT; production uses a 1-ms
tick and 120,000 TIME-WAIT ticks, plus one tick to cover phase uncertainty.
Standalone RTL is in `build/tcp-control-rtl/` and `build/tcp-managed-rtl/`.

These are internal control contracts. The receive allocator supplies a promised
byte window whose right edge must not shrink, and supplies a durable payload
token before submitting metadata. It must retain accepted descriptors and hide
`beforeEstablished` payloads until handshake completion. Transmit reservations
must be scheduled in sequence order; completions report actual wire completion
and the matching generation. The control plane currently returns a repeated
ACK for a receive gap; the out-of-order storage/merge layer is still missing.
A congestion window is supplied externally; congestion control itself is not
yet implemented. ACK aggregation/delay/piggybacking, persist probes, session
lookup/ISN generation, and receive ownership/reclamation remain to be connected.
The sender implements TX ownership/reclamation, but its TCB inputs are still
supplied by the surrounding engine. Timestamps and SACK are parsed but not offered/negotiated by this TCB.

The new data-sender test (`tcp_data`) uses four 2-KiB retained banks and
independently checks complete IPv4/TCP packets against original application
bytes. It covers 2,256 writes (200 rejected), 3,628 reservations, 3,914 emitted
packets, 287 successful replay requests, 916 replay misses and 42 resets over
214,930 cycles. Cases include odd partial-ACK offsets, sequence wrap, credit
smaller than MSS, zero credit, all banks occupied, ACK during replay, retained
bank reuse under stalled output, queued-but-unsent replay rejection, malformed
writes and concurrent input/output. Directed cases also withdraw or change a
speculative replay request and fully ACK its candidate during lookup. The current
hashed verification is a focused `tcp_data` run after scheduler optimization;
connection-control and managed-timer results above are earlier regressions.
This is a sender integration
test with a modeled TCB/physical-completion boundary, not a full TCP peer test.
Default RTL is in `build/tcp-data-rtl/`.

For integration, `TcpConnectionIO.sendCredit` supplies the remaining minimum of
peer and congestion windows after reserved flight. Connect the same TCB's
sequence, oldest-ACK, actual-wire sequence, MSS and generation to the sender.
The reservation channel is an atomic Decoupled allocation request, not an
Irrevocable packet stream: credit changes and replay priority may change its
offer. The TCB must grant a reservation only on a handshake. Replay is limited
to already transmitted, unacknowledged bytes and does not allocate new sequence
space. A block is retained until all its bytes have been scheduled and ACKed.
A partial ACK does not free its partially acknowledged bank.

Replay bank lookup crosses two register boundaries before issuing the read.
Before `replay.ready`, this lookup is speculative: withdrawing/changing the
request cancels it, and ACK/actual-wire sequence/MSS are revalidated on the
handshake. Candidate banks are pinned throughout lookup; read acceptance then
transfers the pin to the payload store. Normal-data reservations retain their
original cadence. Three-way length limits use parallel comparisons, and bank
reclamation no longer feeds back from the combinational read handshake.

Application write blocks are at most one bank each; larger streams use
consecutive blocks. Each accepted write consists of an allocation, packed
payload (including a keep=0,last beat for an empty write), then a store-result
handshake. Bad writes and empty writes consume no sequence space.
The data packet header is sampled on the `prepared` event; TTL must be nonzero.
This sender currently emits ACK/optional-PSH data packets without TCP options.
The `packet` descriptor accompanies every offered IP output beat and must
travel through routing and MAC buffering. Only actual wire completion may
advance the TCB's `sendActual`/timer. Abort/reuse must drain or cancel old wire
descriptors and flush the sender plus downstream queues together; changing the
generation input alone does not flush pending data.

The default retained TX capacity remains eight 2-KiB banks (16 KiB), a
configurable bring-up size, not a guarantee of 10G throughput at arbitrary
RTT. Sustained throughput also needs enough retained flight for bandwidth times
RTT, plus sufficient peer/congestion windows. Shared-memory/session allocation
and sizing for the target board remain part of endpoint integration.

Ideal-input, ready-output TCP data-sender measurements (256 packets/case,
immediate modeled ACKs, excluding startup/drain) are:

| TCP payload bytes | TX cycles/packet | Conditional service capacity at 156.25 MHz |
| ---: | ---: | ---: |
| 1 | 6 | 17.500 Gb/s |
| 64 | 9 | 19.722 Gb/s |
| 1460 | 97 | 19.820 Gb/s |

The conversion includes Ethernet header/padding/FCS, preamble/SFD and IFG for
comparison with wire demand, even though this test terminates at IP output.
It excludes the TCB, Ethernet routing/ARP/MAC stages, physical timing and RTT
effects. It proves sender scheduling headroom only, not full-endpoint line rate.

The integrated Ethernet harness uses 64-beat banks. Its independent software
peer constructs/checks complete frames, answers ARP, and checks payloads under
random independent backpressure. Coverage includes cold resolution, on-link and
gateway destinations, shared-port arbitration, minimum frame padding, bad frames,
MTU rejection, missing gateway, stalled MAC output and retry exhaustion.

Cycle-service measurements (256 packets per case, concurrent RX/TX, cached ARP,
continuous producers and ready consumers) are:

| UDP payload bytes | TX cycles/packet | RX cycles/packet |
| ---: | ---: | ---: |
| 0 | 9 | 7 |
| 1 | 9 | 7 |
| 17 | 9 | 7 |
| 512 | 40 | 38 |
| 896 | 64 | 62 |

These exclude startup/drain latency by measuring first-to-last completion
intervals. Counting frame bytes plus FCS, preamble/SFD and IFG, the smallest
cases correspond to service capacities of 11.667 Gb/s TX and 15 Gb/s RX **if**
the circuit runs at 156.25 MHz. This is a scheduling budget, not measured Fmax,
board throughput or a physical 10G qualification. All tested packets are also
checked byte-for-byte; throughput reporting never replaces functional checking.

The pinned GSIM has a discovered constant-concatenation code-generation issue.
`sim/gsim-known-issues.txt` records the evidence and equivalent wide-field
placement used to avoid it. Generated RTL and independent wire expectations are
retained as separate checks; the vendored simulator is not patched.

## Vivado timing workflow

The board part found in the Valence project is `xczu15eg-ffvb1156-2-i`.
The first baseline uses Vivado 2025.1, a 6.4-ns clock (156.25 MHz), 0.1-ns
setup/0.05-ns hold uncertainty, and explicit boundary delays (max 1 ns, min
0 ns) on every data input/output, including synchronous reset. OOC clock
origin is fixed at BUFGCE_X0Y0. These are reproducible module constraints,
not the final SoC boundary/clock-tree budget; no false or multicycle paths
are added to make reports pass.

`fpga/prepare_ooc.py` verifies the current functional manifest, then freezes
default-geometry RTL, its hashes and the exact timing script into a timestamped
directory under `build/vivado/`. `fpga/ooc.tcl` runs synthesis, placement,
physical optimization and routing, retaining checkpoints, setup/hold paths,
register-only paths, utilization, DRC and methodology reports for
`TcpDataTransmit`, `TcpManagedConnection`, `TcpReceive` and
`EthernetUdpEndpoint`.

Source and Mill/GSIM builds remain in WSL home. Windows Vivado work is staged
under the user-selected `E:\\VM\\Share\\Valence-rtl`, in a separate
`hardware-netstack-ooc-TIMESTAMP` directory. Prepare a fresh snapshot in WSL,
then invoke `fpga/run_windows.ps1 -SourceRun SNAPSHOT_UNC_PATH -Mode Run`
from Windows PowerShell. If UNC script policy requires it, launch that
trusted local script with a process-scoped `-ExecutionPolicy Bypass`.
The launcher refuses to overwrite an existing run. `Stage` copies only frozen
inputs; `Collect` copies completed reports from a run started in WSL.
Vivado uses the Windows snapshot and does not modify the Chisel source tree.

A nonnegative setup result alone is insufficient: inspect hold, pulse width,
unconstrained paths, routing status and DRC as well. A module passing OOC timing
does not establish the timing of the integrated protocol stack or SoC.

The initial run `20261017T145425Z` was stopped at the user's request during
routing of its first top, `TcpDataTransmit`. Its post-synthesis setup WNS was
-4.761 ns; the worst reported path ran from `sendActual[1]` to `fault` through
37 logic levels. These are pre-route results, not final routed timing. Other
tops never ran. The frozen input and partial reports are retained with an
explicit stopped status; no timing pass is claimed.

The subsequent scheduler optimization registers replay lookup, parallelizes
length limiting and removes read-handshake feedback from bank reclamation.
The focused functional regression passed, including the unchanged 6/9/97-cycle
normal-data service measurements above. Vivado has not been rerun for this
revision, so neither a new WNS nor achievement of 156.25 MHz is established.

## Remaining scope and acceptance

1. Add ICMP error/echo handling and path-MTU feedback. Extend the existing
   Ethernet/IP routing and address-filter policy where required.
   Review options/fragments and implement the required host behavior or explicit
   slow-path handling; their current rejection is a fast-path limitation.
2. Connect the TCP TCB to session lookup/ownership, ISN generation and the
   Ethernet/IP/TCP wire datapath. Complete receive reordering and ACK
   aggregation/delay/piggybacking; verify open, data transfer and close end to
   end. Keep all packet handling autonomous.
3. Integrate the implemented TX ACK-reclamation/retransmission scheduler with
   the TCB, control-packet arbitration and physical completion tracking. Add
   receive-credit/window management, persist/zero-window probes,
   congestion control/fast retransmission and connection user-timeout handling.
   Apply MSS/WS negotiation throughout the integrated payload path.
4. Verify complete wire packets against independent peers and reference traces:
   simultaneous sessions, loss, duplication, reordering, malformed segments,
   stalled consumers, buffer pressure, reset, sequence/timer wrap and recovery.
5. Measure sustained RX/TX throughput and small-packet rates. Synthesize and
   check timing/resources for a specified FPGA before claiming 1G/10G line rate.
   The 128-bit datapath is a design choice, not measured timing evidence.
6. Integrate MAC/PHY and run board-level peer tests. Linux/DMA APIs are deferred
   per the user's request and will be designed after protocol behavior works.

The completion criterion remains the hardware TCP/UDP stack, not the checksum
or IPv4/UDP subset. Current successful tests do not establish TCP compliance,
10G timing, interoperability, or operation on the board.

## Protocol references

* [RFC 1071 — Internet checksum](https://www.rfc-editor.org/rfc/rfc1071)
* [RFC 768 — UDP](https://www.rfc-editor.org/rfc/rfc768)
* [RFC 791 — IPv4](https://www.rfc-editor.org/rfc/rfc791)
* [RFC 1122 — Internet host requirements](https://www.rfc-editor.org/rfc/rfc1122)
* [RFC 826 — ARP](https://www.rfc-editor.org/rfc/rfc826)
* [RFC 9293 — TCP](https://www.rfc-editor.org/rfc/rfc9293)
* [RFC 5961 — TCP reset/SYN/ACK validation](https://www.rfc-editor.org/rfc/rfc5961)
* [RFC 5681 — TCP congestion control](https://www.rfc-editor.org/rfc/rfc5681)
* [RFC 6298 — TCP retransmission timer](https://www.rfc-editor.org/rfc/rfc6298)
* [RFC 7323 — TCP window scaling and timestamps](https://www.rfc-editor.org/rfc/rfc7323)
