# Pinned dependency setup and redistribution limits

The public repository does not contain the original openfpgaSDK, openfpgaCore or SpinalHDL Git bundles. Their histories include Roland-derived sample banks without an upstream redistribution license, and the SDK contains third-party demo music with unverified rights. These are not PAL game assets and are not required for PAL OGG music, but leaving them inside a Git bundle would still redistribute them. They are explicitly excluded here.

The relevant upstream declarations are openfpgaSDK `NOTICE` and `LICENSES/LicenseRef-Proprietary-SampleData.txt`, with separate third-party-media notices. Preserve all applicable upstream licensing obligations. This source publication does not grant rights to the omitted media.

SpinalHDL is also externalized: its bundled Micron DDR and Xilinx simulation models have vendor copyright notices without a redistribution grant established during this publication review. The main library licenses do not by themselves establish the rights for every vendored model.

## Required local inputs

Obtain the upstream dependencies under the applicable licenses and permissions, then provide clean local Git checkouts at these exact commits:

- openfpgaSDK: https://github.com/openfpgaOS/openfpgaSDK.git
  `a408ddc12aed0dfaa4aa22c06af82f829db77126`
- openfpgaCore: https://github.com/openfpgaOS/openfpgaCore.git
  `618a3eb985759a4154115109c2c8036271252888`

- SpinalHDL: https://github.com/SpinalHDL/SpinalHDL.git
  `6f8510cdbb8ad7b8bcc0f6d58395669c4c4d7e2d`

The bootstrap script will only use paths you explicitly pass with `--sdk`, `--core` and `--spinal`. It checks exact HEAD and clean status before creating the output workspace, then clones those local repositories with file transport only. It does not fetch those dependencies, access credentials, install software or make a license decision for you.

```sh
python3 reconstruct_inputs.py
python3 bootstrap_sources.py --verify-only
python3 bootstrap_sources.py --workspace ./work \
  --sdk /path/to/openfpgaSDK --core /path/to/openfpgaCore --spinal /path/to/SpinalHDL
```

`--verify-only` verifies the included handoff files. It does not claim the external checkouts are installed or validated; those are verified during materialization.

VexiiRiscv and rvls bundles and shallow-boundary sidecars are included at the pins in handoff-manifest.json. The pinned musl 1.2.5 source archive is included. Large included inputs must first be reconstructed from the hash-checked parts. Toolchains, Quartus, Docker images and sbt caches remain separately provisioned build prerequisites.

The original 2026-10-05 offline ZIP is not the public repository layout: the source publication omits the three SDK/Core/SpinalHDL/SpinalHDL bundles, legacy runtime candidates, unrelated MiSTer artifacts and internal verification logs. No newly compiled bitstream or hardware validation is implied.
