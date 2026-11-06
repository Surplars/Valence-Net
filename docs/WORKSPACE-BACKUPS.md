# Local workspace backup map (2026-11-07)

All branches are in https://github.com/Surplars/Valence-Net.

| Local directory | Remote source |
| --- | --- |
| Valence-Net | main |
| Valence-Iperf-Rx256K | n57b-tested |
| Valence-Iperf-Rx256K-Main | n57b-main |
| Valence-Iperf | archive/iperf-history-20261107 / Valence-Iperf |
| Valence-Iperf-Net57c6984 | archive/iperf-history-20261107 / Valence-Iperf-Net57c6984 |
| network-pll250-r3-20261019 | archive/iperf-history-20261107 / network-pll250-r3-20261019 |

The [historical archive](https://github.com/Surplars/Valence-Net/tree/archive/iperf-history-20261107)
is committed as 8a2bd3011b0a9e5d43fdd44642e5c41c33cfa166. A fresh remote clone
verified 64 original Valence-Iperf files, 67 original N57A files and the
66,664-byte PLL recovery tarball against SHA256. Original source/configuration/
documentation files are preserved exactly. Dependency pins, restoration helpers,
the historical TX32 comparison model and available validation summaries are included.

The original N57B archive previously passed an 81-file hash check. A fresh
N57M remote clone restored its exact dependency/tool sources and compiled.
See [RESTORE.md](https://github.com/Surplars/Valence-Net/blob/n57b-main/RESTORE.md)
and [the source merge record](N57B-MERGE.md).

This preserves sources and selected verification records, not whole directories.
build/out caches, generated executables, bitstreams, DCP checkpoints and raw
measurement logs are not completely backed up; retain required exact artifacts
separately before deleting local workspaces. The PLL directory's transfer-service
metadata is excluded, while its actual recovery archive is preserved.
