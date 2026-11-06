# Hardware-tested N57B source archive

This branch preserves the original /home/openion/Valence-Iperf-Rx256K source.
All 81 source/configuration/documentation files match the recorded originals;
see docs/preserved-verification/original-source-sha256.json.
The two netstack override files and the 57c6984 dependency pin are intentional.

Clone the n57b-tested branch and run python3 scripts/bootstrap.py --ssh.
The n57b-main branch contains the upgraded application and portable tool/TX32
restoration instructions in RESTORE.md. Neither branch depends on retaining
the original source directories. Upstream dependency commits are recorded.

The four saved validation JSON files are historical records. Original external
paths inside them are provenance, not files included here. Generated executables,
Vivado DCPs and the programmed N57B bitstream are not archived in this branch.
Retain those separately if exact image/checkpoint preservation is required.
No new timing or physical-throughput validation is claimed.
