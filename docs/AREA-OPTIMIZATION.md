# Area optimization, 2026-10-24

## Scope and baseline

The baseline is commit `89cefc82f6be9f0e2f0467aaf0e413f6a2116307`.
Keep four TCP contexts, all default buffer capacities, SACK, ICMP/PMTU,
128-bit streams and the 156.25 MHz target. MAC/SoC adaptation remains deferred.
This round changes implementation structure, not capacity or supported features.

The baseline production core used 185,327 LUTs after synthesis
(`build/vivado/20261024T030357Z`) and 184,655 after routed refinement
(`docs/timing/2026-10-24-042645`). Synthesis and routed figures must be compared
at the same stage. Original clock/uncertainty are 6.4/0.100/0.050 ns; no
false-path or multicycle exceptions are introduced.

## Changes

- SACK snapshot scratch storage is two interleaved, single-write RAMs. Adjacent
  sort entries always occupy different banks, allowing an exchange with one
  write per bank. Atomic input-capture registers protect the snapshot while
  live descriptors change; scratch RAM is not reset or published directly.
- Bubble passes stop at the already sorted suffix. Merged intervals feed a
  stable top-four insertion structure in recency order. Equal-age ties retain
  the original sequence order. Clear and dirty work suppress publication.
- Initial SYN data uses its known zero overlap and 16-bit unscaled window.
  This removes general modular overlap arithmetic from that special case,
  preserving clipping, failed-storage behavior, FIN acceptance and wrap.
- Public elaboration-time parameters and remaining fixed geometry are listed
  in [PARAMETERS.md](PARAMETERS.md). No default geometry was reduced.

The scratch RAM's mapped primitive style and net area benefit must be read from
Vivado reports. This source-level RAM change alone is not evidence of BRAM use.

## Functional evidence

The new sack_snapshot test uses independent software sorting, adjacency merging
and stable recency selection over 2,688 cases. It covers invalid entries,
sequence wrap, age ties, atomic capture, newer dirty work and mid-build clear.
The same oracle also passes against the original 89cefc8 RTL in a native
isolated copy: 210,690 cycles versus 151,482 for the candidate, including test
setup and superseded work. Maximum settle time in that test is 119 versus 77
cycles. These are cold snapshot test costs, not wire-throughput figures.

The new handshake_body test covers active/passive SYN payload clipping,
unscaled window bounds, failed storage, FIN edges and 32-bit wrap.

The complete 44-target ASan/UBSan regression passes against the exact 104-file
source set in build/verified.json. Log: build/area-full44-verified-regression.log.
Its 42 existing targets retain all independent byte/checksum expectations.
Forty result strings are unchanged; cold-SACK scheduling changes the adaptive
single-Ethernet trace and adds 24 cycles to the 61k-cycle reassembly test.
The sustained Ethernet service targets retain their original 10G budgets.

Current-source production probes also pass: a 64 KiB RX ring serves
1/64/512/896/1460-byte TCP payloads in 9/11/39/63/98 cycles; reassembly delivers
315,360 bytes across 24 waves in 131,014 cycles. The single-session default
closed-response path passes unknown/live/listener and stalled-metadata checks.
Full 1/3/8-context production configurations elaborate without warnings.
Separate 2/16/256-bank snapshot geometries elaborate; the two-bank case reports
two single-element dynamic-index warnings. The load counter remains zero in
that case. It is not the production geometry, and those parameter probes are
elaboration-only evidence.

## Synthesis comparison

| Core resource | Baseline | Candidate |
| --- | ---: | ---: |
| Total LUTs | 185,327 | 181,021 |
| Logic LUTs | 176,199 | 171,573 |
| LUTRAM LUTs | 9,128 | 9,448 |
| FFs | 90,078 | 90,378 |
| RAMB36 / RAMB18 | 136 / 20 | 136 / 20 |
| URAM / DSP | 0 / 0 | 0 / 0 |

This is a **4,306 LUT / 2.323% net reduction** at the same post-synthesis stage.
The four SACK snapshot instances together fall from 13,844 to 9,222 LUTs
(33.386%). Other logic can be remapped by synthesis, so the whole-core totals
are the net area comparison. FF use increases by 300; LUTRAM use by 320.
Vivado selects LUTRAM for the 4-by-69-bit scratch banks. There is no claim that
this round moves the scratch storage into BRAM or uses previously idle URAM.

## Routed checkpoint refinement

The complete default run 20261024T061030Z saved baseline_route.dcp with
setup -0.014 ns, hold +0.005 ns and zero hold violations. Local physical
optimization reached rounded WNS 0.000 / TNS 0, but its subsequent global
Explore pass oscillated on a few congested nodes. That parent flow was stopped
and is explicitly recorded as incomplete.

fpga/prepare_saved_refinement.py freezes the immutable saved stage, its timing
report hash, original RTL provenance, constraints and checkpoint SHA. It does
not mark the parent complete. fpga/refine_routed.tcl first independently checks
full routing, then performs local physical refinement and produces a separate
final report/checkpoint. The restored stage has all 241,103 routable nets
fully routed and zero routing errors. Preparer fixtures reject both an older
source revision and a tampered RTL input. These rejection tests are tool checks,
not hardware timing evidence.

The independent run build/vivado/20261024T074147Z passes all routed checks.
Its first attempt uses the original 0.100 ns setup uncertainty; optional
stricter optimization attempts are not used. Required signoff remains
6.4 / 0.100 / 0.050 ns. Setup is 0.000 ns, hold +0.005 ns, pulse width +2.658 ns.
Setup and hold each cover 267,119 endpoints with zero failures and TNS/THS zero.
All 241,104 routable nets are fully routed, with zero routing errors. All twelve
coverage checks are zero, with no blackboxes or DRC/methodology/tool errors.

| Fully routed core resource | Baseline | Area revision |
| --- | ---: | ---: |
| Total LUTs | 184,655 | 180,442 |
| Logic LUTs | 175,555 | 171,022 |
| LUTRAM LUTs | 9,100 | 9,420 |
| FFs | 90,065 | 90,361 |
| RAMB36 / RAMB18 | 136 / 20 | 136 / 20 |
| URAM / DSP | 0 / 0 | 0 / 0 |

The net routed reduction is **4,213 LUTs / 2.282%**, with 296 more FFs and
320 more LUTRAM LUTs. This is the final same-stage area comparison.
Published originals, full functional manifest, source/RTL/checkpoint lineage,
hashes and warning review are under [timing/2026-10-24-074147](timing/2026-10-24-074147/artifact-hashes.json).
The setup margin rounds to zero after the required clock uncertainty; SoC
integration needs renewed timing closure. No SoC, MAC/PHY or board throughput
signoff is included.
