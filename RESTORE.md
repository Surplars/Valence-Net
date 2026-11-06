# Restore N57M after removing local workspaces

The application is on the **n57b-main** branch of Surplars/Valence-Net.
The protocol stack remains on main. The hardware-tested original application
is separately archived on **n57b-tested**.

## Native WSL checkout

Keep Git, sources, Mill and simulation under the WSL home filesystem:

```sh
git clone --branch n57b-main --single-branch git@github.com:Surplars/Valence-Net.git ~/Valence-Iperf
cd ~/Valence-Iperf
python3 scripts/bootstrap.py --ssh --tools
```

This restores exact Valence/Valence-Net pins from dependencies.json, upstream
iperf3 and GSIM 93b8cd23. The recorded GSIM path in the original dependency
metadata is historical; the bootstrap and simulator use GSIM_SOURCE when
provided, otherwise ~/.cache/valence-net/gsim-src-93b8cd23.

Install Mill 1.1.7 (also pinned in .mill-version), a suitable JDK, Clang 19,
GNU Make, a C/C++ build toolchain and GMP development headers. Ubuntu 24.04
was used for the saved validation. Scala 2.13.17 / Chisel 7.3.0 are pinned
in build.mill. Build the restored tools:

```sh
export GSIM_SOURCE="${GSIM_SOURCE:-$HOME/.cache/valence-net/gsim-src-93b8cd23}"
make -C "$GSIM_SOURCE" -j4 build-gsim
cd tools/iperf3-src
./configure --disable-shared
make -j4
cd ../..
```

Then:

```sh
mill -i hw.compile
python3 sim/build.py gmii
python3 sim/build.py rxqueue
build/rxqueue/test
python3 scripts/restore_tx32.py
python3 sim/test_transport.py
```

The historical TX32 reference model is stored in sim/fixtures/tx32-model.tar.xz.
Its three original FIR/C++/header SHA256 values match the published merge
evidence. Restoration verifies the archive, every member and any existing
destination before writing. test_transport.py restores it automatically if
the old local model pointer is absent. No old /home/openion project is needed.

For application-only / real Linux TCP interoperability, build app or gmii
with sim/build.py and use the existing sim/test_app.py,
sim/test_protocol_negative.py and root-only sim/test_tap.py. The TAP test
uses a temporary isolated network namespace. These are separate checks,
not a new hardware-throughput or bitstream claim.

## What is preserved

- Full application, MAC adapter, FPGA wrappers/constraints, tests and scripts.
- Exact upstream dependency pins and the 7f651a6 protocol-stack merge.
- The original local application commit 2cd2da4 in this branch's ancestry.
- Source-bound validation records, including the dependency commit binding.
- A compressed historical TX32 model for the existing 13-case comparison.
- Original N57B source and historical records on n57b-tested.

Generated build/out caches, executables, Vivado DCPs and bitstreams are not
Git source artifacts. They must be rebuilt or backed up separately if the
exact previously implemented/programmed image is needed. The saved N57B
timing summary does not reproduce its physical checkpoint or bitstream.
N57M has functional validation only; its physical timing and board throughput
still require new qualification.
