# Workspace retention decisions (2026-11-07)

Keep useful implementations and tested baselines; superseded workspaces and
temporary transfer files do not need separate maintained archives.

| Local directory | Decision |
| --- | --- |
| Valence-Net | Keep source on main |
| Valence-Iperf-Rx256K | Keep the hardware-tested N57B baseline on n57b-tested |
| Valence-Iperf-Rx256K-Main | Keep the upgraded N57M application on n57b-main |
| Valence-Iperf | Superseded 128 KiB application; no separate archive needed |
| Valence-Iperf-Net57c6984 | Superseded N57A 128 KiB application; no separate archive needed |
| network-pll250-r3-20261019 | Retain only the small experimental recovery tarball below |

The two older applications have no hardware-source module missing from N57B.
Their Scala differences are the 128/256 KiB capacities, corresponding window
scale/test-shim constants, and the earlier interface without PMTU diagnostics.
Their whole project copies are unnecessary for rebuilding the retained versions.
The temporary archive/iperf-history-20261107 branch was removed after this review.

The [PLL250 r3 reference](recovery/pll250-r3/README.md) includes a dedicated
clock module/integration patches not present in the pinned MAC. Only its
66,664-byte original tarball is retained, with its SHA256 and unverified physical
status. Download-service metadata and helper scripts have no project value.

The retained N57B archive passed an 81-file original-source hash check.
A fresh N57M remote clone restored exact dependency/tool sources and compiled.
Its existing 13-case transport comparison retains the historical TX32 model,
which has ongoing regression value.
See [RESTORE.md](https://github.com/Surplars/Valence-Net/blob/n57b-main/RESTORE.md).

Local source copies can be recreated from the retained branches. This is not
a complete disk backup: preserve any required exact bitstream/DCP separately.
Generated build caches and obsolete raw logs are not retained by default.
