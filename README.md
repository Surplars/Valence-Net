# N57M: N57B application on current Valence-Net

Separate source migration; the original N57B workspace and bitstream are preserved.
Two TCP contexts, 64 TX banks, 256 KiB RX/context, window scale 3, and the N57B
pressure-window ACK policy are retained. Local protocol-stack overrides are removed.
The current shared receive frontend and selective memory queues are used.

This is a 1G GMII board integration. New timing, bitstream and board throughput
are unverified. Historical N57B records are in docs/n57b-original and do not
qualify this source. CANDIDATE.json records exact dependency source hashes;
if net_worktree_dirty is true the commit alone does not identify the source.
The tools symlink and old TX32 comparison model are local test dependencies.

Run python3 sim/build.py gmii, python3 sim/build.py rxqueue,
build/rxqueue/test and python3 sim/test_transport.py in native WSL.
The merged Valence-Net report describes the tests actually completed.

Current dependency commit: 7f651a6185e3dd13f9981b2ae024d3b6c117f588. The dependency checkout is clean.
Nine standalone targets, the RX-window oracle and 13 GMII transport cases pass.
TX64 model throughput is 949 Mbit/s at ACK delays of 256/512/640 us.
The model manifests and dependency-commit-binding.json retain the validation history.
