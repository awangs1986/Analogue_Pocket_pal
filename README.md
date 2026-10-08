# SDLPAL for Analogue Pocket

Development source and reproducible build handoff. **Not a hardware-validated release.**

- Actual Pocket C platform and FPGA display-mode RTL patch: [source/pocket](source/pocket).
- SDLPAL engine: [source/sdlpal](source/sdlpal), GPL and original third-party notices preserved.
- Native 320×200 indexed game surface, default 4:3 scaler display.
- Ogg/Vorbis decoding runs on the RISC-V CPU; the FPGA outputs PCM.
- New display-mode bitstream has not completed Quartus synthesis/fit/assembly/timing or physical Pocket/Dock acceptance. Historical host/ELF tests do not establish those results.
- No proprietary game assets, credentials or ready-to-flash release are included.

## Restore and build

Large font/decoder/source-dependency inputs are losslessly stored in bounded parts. After cloning, run:

```sh
python3 reconstruct_inputs.py
python3 bootstrap_sources.py --verify-only
python3 bootstrap_sources.py --workspace ./work --sdk /path/to/openfpgaSDK --core /path/to/openfpgaCore --spinal /path/to/SpinalHDL
cd work/mister-pal-pocket
```

Read [BUILD_SPEC.md](BUILD_SPEC.md) and [the build handoff](README_构建交接.md) before building. The existing build driver requires locally provisioned tools/images and never installs Quartus, pulls images, flashes hardware or supplies commercial game data. VexiiRiscv/rvls source bundles and musl are included in the reconstructed dependencies. SDK/Core/SpinalHDL must be supplied as clean local checkouts at their exact pins: their upstream histories contain third-party sample media without redistribution permission, so those bundles are deliberately excluded. See [dependency setup and exclusions](DEPENDENCIES.md). Compiler/Quartus binaries, Docker images and sbt caches are not included.

The original handoff also described separate old v1 candidates; these are intentionally not published as runtime releases here. Follow the source-build instructions.

See [provenance](SOURCE_PROVENANCE.md), [exact input manifest](handoff-manifest.json), [stored-parts inventory](input-parts.json) and [licenses](source/LICENSE.md).
