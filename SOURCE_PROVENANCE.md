# Source provenance

Published from the SDLPAL Pocket build handoff dated 2026-10-05. Original ZIP SHA-256: 8a728b823916d458d862ac4d0092028a23e9513ecd6eda5f33c0b72764f389d1.

Engine baseline: https://github.com/awangs1986/mister_pal/tree/1d35d896b99722b9968685c1284cb5f1566cd662 . Source is an exported snapshot, not reconstructed Git history. The included VexiiRiscv and rvls bundles preserve exact pinned histories and shallow boundaries. SDK/Core/SpinalHDL are externalized as described in DEPENDENCIES.md; their original pins remain recorded in handoff-manifest.json.

The public source selection includes the Pocket target, SDLPAL engine, redistributable source dependencies, build driver, RTL patch, tests and licenses. It omits unrelated legacy MiSTer source/output/deployment workflows, unused desktop signing and icon resources, old candidate binaries and internal verification logs. Original exclusion names are recorded in the manifest. Historical documentation filesystem examples are generalized. No proprietary PAL game assets are included.

Keep all per-file license headers, source/LICENSE, source/NOTICE.md, source/sdlpal/LICENSE and source/pocket/licenses. Font sources retain upstream GNU Unifont/GPL, Red Flag BSD-style, or GNU intlfonts/CHDRV public-domain provenance claims. Dependency repositories retain their original licenses.

Large inputs are stored losslessly as bounded compressed parts to make transfer reliable. reconstruct_inputs.py restores their exact original bytes and verifies SHA-256 before bootstrap. These parts are project source/dependency inputs, not a ready-to-flash runtime.
