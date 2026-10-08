# Patched runtime build status, 2026-10-05

## Outcome

The official Quartus Lite 25.1 and Cyclone V downloads passed the vendor's
published SHA-1 checks. Installation did not succeed: the actual unattended
installation exited with SIGSEGV before producing output or creating its
installation directory. Quartus synthesis, fitting, assembly and timing
analysis have **not run**. There is no newly built display-mode bitstream.

The two download-page supplemental agreements were explicitly accepted after
user approval. The installation attempt included the authorized
`--accept_eula 1` option, but the immediate crash means successful installer
acceptance was not observed. These are distinct events.

## Verified inputs

| Input | Bytes | SHA-1 |
| --- | ---: | --- |
| `QuartusLiteSetup-25.1std.0.1129-linux.run` | 1,982,715,698 | `ce0773469eacab5b7035c175484625f4ec3737d1` |
| `cyclonev-25.1std.0.1129.qdz` | 1,443,834,466 | `a7225ec1bd36ccfd6826ea6273df5d21dd95633b` |

Both files are retained in `/path/to/local/quartus-25.1-downloads/` in the
current cloud workspace. They are proprietary installation media and are not
included in the source/probe deliverables.

Prepared source is `/path/to/local/openfpgaCore-pal-display`, based on
`618a3eb985759a4154115109c2c8036271252888` with the patch identified in
`pocket/fpga/source.json`. Pinned dependencies were fetched read-only; no
fallback to an unpinned branch was needed:

- VexiiRiscv: `580b76c3868512c8316bb7a3d3add81cad49a0dc`
- SpinalHDL: `6f8510cdbb8ad7b8bcc0f6d58395669c4c4d7e2d`
- rvls: `94f850e5f7c9a36e5aceb3810d658fffae55b497`

## Preparation that passed

The real `os25` VexiiRiscv netlist and RISC-V firmware were generated in the
separate patched tree. The boot image was byte-verified using
`pocket/fpga/verify_boot_image.py`, including NOP padding. These intermediate
outputs have not replaced any file in the original SDK or the v1 package.

| Prepared output | SHA-256 |
| --- | --- |
| `VexiiRiscv_os25.v` | `031ec529cd4e8b5482b1bfac500ed848488927d1c44d183748fb2900dea6a216` |
| `boot.bin`, 15,480 bytes / 3,870 words | `d15c7c0ffee48fcee0ee3e5870f6aff2cded85ba9efe6ba3eb9e786c8408308e` |
| `firmware.mif`, 8,192-word depth | `7affb5786d4673ad8573d70cf1a83c4947c7595c7f27daa49fbc3edbbe22e251` |
| `os.bin` | `b50ac30d325b9bc9c52b303ed05e1063069527d41dedfd524e73a9ce8bb41507` |

The isolated Quartus job `os25-display-s40` has a generated QSF for device
`5CEBA4F23C8`, seed 40 and four Quartus processors. The 100 MHz target was not
lowered. Its absolute source paths are local to this workspace, so regenerate
the QSF after moving the tree rather than reusing it blindly. Its current
SHA-256 is `019131509ecb76bccd16abf16fb8d31a0ddbe9ed5fd1aacc44ed86c01cf337ba`.

## Installation failure and limits

The cloud host reports x86_64, Debian 13, glibc 2.41 and kernel 6.18.44. About
22 GB was available at the attempted installation. The terminal attempt was:

```sh
QuartusLiteSetup-25.1std.0.1129-linux.run \
  --mode unattended --unattendedmodeui none \
  --installdir /path/to/local/toolchains/quartus-25.1 \
  --accept_eula 1
```

It used isolated HOME/TMPDIR and returned signal 11 with zero output bytes.
Help/text-mode, clean-environment and loader probes also failed. One isolated
official Ubuntu 22.04 libc/loader probe failed without replacing host
libraries. No vendor binary patch, security-setting change, container-runtime
installation or system-driver installation was performed. `strace` was denied
by the environment's ptrace restriction; that denial was not bypassed.

The cause remains unresolved. Neither Debian/glibc compatibility nor sandboxing
has been proven to cause the crash. Full evidence remains at
`/path/to/local/quartus-25.1-reports/build-blocker.json` and
`installer-unattended-result.json` in that directory.

## Resume gate

Use an authorized, working Quartus 25.1 environment, preferably a complete
vendor-supported Ubuntu 22.04 x86_64 environment, or obtain vendor diagnosis.
The [Altera OS matrix](https://www.altera.com/design/guidance/software/os-support)
lists Ubuntu 22.04; the current cloud host's Debian 13 is not listed. A small
libc bundle does not constitute a supported Ubuntu installation.

Read the pinned core's `CLAUDE.md` before resuming. Its shipping workflow uses
isolated containers. In this revision, `USE_QUARTUS_CONTAINER=0` does **not**
make `make build` a native build: that target still invokes
`quartus-container.sh`. Native diagnostic builds, if deliberately selected,
need the generated job QSF and direct `quartus_map`, `quartus_fit`,
`quartus_asm`, `quartus_sta` calls with private HOME/TMPDIR; they must not be
misrepresented as the upstream container-proven release workflow. No such
native Quartus commands have run here.

After the environment is ready, follow the complete
[build and release gates](display-modes-followup.md#build-and-release-gate).
Retain full setup/hold reports, resources, warnings, tool versions and coherent
runtime hashes. Do not claim timing closure based on source simulation,
firmware compilation or another project's bitstream. Pocket/Dock display
behavior and OGG decode performance still need actual device measurements.

The existing v1 and source-only Library deliverables remain unchanged. Color
LCD declarations are configuration-ready and hardware-unverified; monochrome
RTL is implemented and simulated but additionally blocked on FPGA build and
timing verification. The complete filter requirement remains open.
