# PLL250/startup r3 experimental reference

Retained from the 2026-10-19 recovery package because it includes a dedicated
PLL250 clock module and integration patches absent from the pinned iperf MAC.
This is reference material, not part of the current protocol-stack/application build.

Only the original 66,664-byte recovery tarball is retained. SHA256:
84a98e0ecb581b4b1f9b6fd9e22fb7004daeb27783152a26eba7a9747cdd2ac7

The package records finite source checks and query mocks. Its receipt explicitly
reports vendor_execution=false and physical_qualification=false. Two board helpers
already match the current pinned MAC; other patches are experimental and must be
reviewed against their exact preimages before use. This is not an approved current
board timing/clock solution.

Temporary transfer scripts, signed download URLs and duplicate iperf workspaces
are not retained with this reference.
