# N57B receive-window integration (2026-11-06)

The hardware-tested application in `/home/openion/Valence-Iperf-Rx256K`
pins Valence-Net at `57c698496c55f3afe6c882f7a8ab9fb041f6b66b`.
It is a standalone source copy without a top-level Git repository. Its clean
dependency checkout contains two locally overridden receive modules selected
by its Mill build. The current upstream base is
`c2b3bba8d39ede70159e37086702af85fc541187`; N57B therefore predates shared
receive validation and selective BRAM/URAM mapping.

## Changes carried forward

- Permit power-of-two receive rings from 4 through 16,384 128-bit words,
  up to 256 KiB per connection. The default remains 4,096 words / 64 KiB.
- Size the released-byte counter from ring capacity, including its full
  capacity value, instead of fixing it at 17 bits.
- Add the elaboration-time `pressureWindowUpdates` option, default false,
  through the single/multi IPv4 and Ethernet endpoints. With it enabled,
  fast readers with more than half the ring free suppress redundant
  application-drain window notifications. Once free space reaches half,
  notifications continue until full drain. Zero-window reopening and SACK
  notifications remain active; packet ACKs and storage ownership are unchanged.

The ACK option ports N57B's policy without changing the default stack policy.
It can leave a partially freed, still-large window unadvertised until the next
normal ACK. It is intended for the measured streaming-reader configuration,
not a claim that every workload benefits from fewer notifications.

The merge retains `MemoryQueue` in the receive ring and all shared-parser,
generation, staging and memory-export changes from c2b3bba. Copying N57B's
whole ring file would have reverted its queue wrapper.

## Application configuration and migration

```scala
new MultiEthernetTcpUdpEndpoint(
    connectionCount = 2,
    txBanks = 64,
    rxWords = 16384,
    pressureWindowUpdates = true)
```

The application still supplies window scale 3 and a 131,072-byte congestion
cap. Window scaling is negotiated at OPEN, not inferred automatically from
`rxWords`. TX bank capacity, MSS, control/data context ownership and the
physical-completion adapter are unchanged.

A separate upgraded project is prepared at
`/home/openion/Valence-Iperf-Rx256K-Main`, with UART build label
`TX64-RX256K-N57M`. The original N57B sources, dependency pins and build
outputs are preserved. Recreate a new independent copy with:

```sh
python3 bench/prepare_n57b.py \
  --source /home/openion/Valence-Iperf-Rx256K \
  --output /home/openion/Valence-Iperf-Rx256K-Main-new
```

The script verifies the original dependency pins and known override hashes,
removes the two shadow copies, enables the ACK option explicitly and uses
the current memory exporter for ZU15EG. It records exact effective network
source hashes; a dirty development snapshot is explicitly distinguished
from its base commit. The upgraded application keeps the original UART
length while changing its final build-label character. Existing historical
documents are archived and do not certify the upgraded source.

## Verification scope

All nine focused targets passed with the pinned GSIM and Clang 19 ASan/UBSan.
The upgraded application passes its unchanged receive-window oracle and all
13 full GMII/TCP/iperf3 transport comparisons. At ACK delays of 256/512/640 us,
TX64 delivers **949 Mbit/s simulated payload throughput** in each case.
The 512-frame forward case checks 747,520 bytes, 513 ACKs and a maximum
262,104-byte advertised window at scale 3, with zero busy/MAC drops.
The migration script reproduces all 77 copied/adapted files exactly.

Current validation results are recorded in
[the merge evidence](verification/2026-11-06-n57b/evidence.json).
The focused standalone checks exercise the new capacity/policy, default
rings/endpoints, reassembly, SACK, window scaling and four-context Ethernet
TX/RX service. The imported receive oracle also checks pressure-latch release
after full drain and clearing retained data before a new generation.

All 110 core SystemVerilog modules from the prior frozen routed input
match the current default production export after removing comments and
whitespace; the original 111 frozen file hashes (including the fixture)
were verified first. This is a source-level comparison,
not formal equivalence or a new implementation run.

N57B/N57M's board application is **1G GMII at 125 MHz**. Its existing
bitstream timing and measured throughput do not qualify the upgraded shared
receive implementation. No new Vivado implementation, bitstream programming
or physical-peer throughput measurement is part of this source merge.
The stack's 10G simulation checks and this 1G application check are reported
separately. Protocol limitations remain in [PROTOCOL-STATUS.md](PROTOCOL-STATUS.md).
