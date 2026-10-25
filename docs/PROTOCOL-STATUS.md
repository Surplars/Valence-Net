# Protocol scope

The endpoint is a bounded IPv4 TCP/UDP implementation for explicitly allocated
hardware connections. It is not a complete general-purpose IP stack or a
drop-in Linux socket implementation. The functional manifest intentionally
records `complete_protocol_stack: false`.

| Area | Implemented | Remaining boundary |
| --- | --- | --- |
| Ethernet / ARP | Ethernet II, padding, frame validation, ARP cache/retries/expiry, subnet/gateway selection | MAC/PCS/PHY, FCS/preamble/IFG and CDC are external; no VLAN |
| IPv4 / UDP | IHL=5, lengths/checksums/destination checks, UDP receive/transmit, optional static port allowlist | No IP options, fragmentation/reassembly, IPv6, forwarding or dynamic routing |
| TCP sessions | Active/passive/simultaneous open, close/reset, half-close, TIME-WAIT, MSS/window scaling | Explicit 1–8 context slots; no automatic listener backlog/context allocation; application supplies ISNs |
| TCP receive | Durable byte ring, eight bounded reorder banks, trimming/reassembly, wrap and receive-window updates | Overflow/unsupported overlaps rely on sender retransmission; reorder snapshot/replay has bounded service |
| TCP transmit / recovery | Retained payload, segmentation, partial cumulative ACK replay, physical-completion ownership, RTT/RTO, conservative Reno, persist | No NewReno/ECN/DSACK, negotiated timestamps, delayed-ACK aggregation or complete RFC 6675 Pipe/IsLost/rescue |
| SACK | Negotiation, four receiver blocks, bounded selective hole recovery; cumulative ACK alone frees payload | Advisory bounded scoreboard; RTO recovers reneging; no full RFC 6675 claim |
| ICMP / PMTU | Echo, selected validated advisory errors, bounded unreachable/reset responses, TCP retained-data repair and UDP MTU feedback | No PLPMTUD, automatic ICMP-black-hole fallback, ICMP authentication, Redirect policy or router error generation |
| Applications / SoC | Per-context streams, commands, notices, ordered completion contracts | DMA/CSR/interrupts, driver/API and Linux socket integration remain separate work |

TCP out-of-order reassembly is present. IPv4 fragment reassembly is absent;
these are different layers.

The shared receive frontend validates full packets before any connection
state changes. Every admitted packet carries an immutable slot/generation tag
through packet RAM, checksum rejection and header removal. Each context keeps
its own committed payload staging, byte ring, reorder retention, TCB, congestion
policy and retransmission ownership. Busy contexts have bounded isolation;
all packet queues are finite, and a MAC-side buffer must absorb internal
backpressure because the wire itself cannot stop.

The 10G qualification covers the documented, independently checked traffic
cases with ready consumers, cached ARP and modeled physical completions.
It does not certify arbitrary option-heavy packets, sustained worst-case
reordering, exception response rate or performance for every parameter choice.
The 128-bit × 156.25 MHz datapath has 20 Gb/s raw bandwidth; this alone is not
a 20G Ethernet-throughput guarantee.

Before calling the stack production-ready, add the SoC/MAC adapter and run
physical-peer interoperability, loss/reorder/congestion stress and long-duration
resource/ownership tests. See [integration contracts](INTEGRATION.md) and
[parameter limits](PARAMETERS.md).
