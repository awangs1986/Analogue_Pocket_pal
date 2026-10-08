> Public repository adaptation (2026-10-08): this document describes the original build handoff. Before following its commands, run reconstruct_inputs.py and see DEPENDENCIES.md. SDK/Core/SpinalHDL bundles and old runtime candidates are not redistributed here; bootstrap requires explicit --sdk, --core and --spinal local checkout paths. No hardware-ready runtime is included.

# SDLPAL → Analogue Pocket：统一构建 SPEC

版本：2026-10-05。本文是接手编译、修改和验收的入口；原始 MiSTer 用法仍在源码树的 `README.md`。
最快命令见 [README_构建交接.md](README_构建交接.md)，逐项记录见源码树的
`pocket/docs/build-handoff-acceptance.md`。统一 ZIP 中源码树位于 `source/`，恢复后位于 `work/mister-pal-pocket/`。

本次实际测试与迁移重建记录见 [验证报告](source/pocket/docs/handoff-validation-2026-10-05.md)。

## 1. 交付目标与当前边界

交付的是完整引擎/平台源码、精确依赖源码、构建驱动、测试与旧版开发候选。
接手者可在合适环境中继续编译修改，不需要等待原环境修好 Quartus。
**这不是已经完成真机验收的新 FPGA 发行版。**

| 项目 | 当前实现与证据 | 仍需完成 |
| --- | --- | --- |
| PAL 引擎 | 完整 `sdlpal/`；实际 RV32 ELF 已交叉编译、静态链接 | 新环境重建；合法数据下完整游戏测试 |
| 画面 | 原生 320×200、8-bit indexed；slot 1；默认 4:3 normal/CRT | Pocket/Dock 实际比例与视觉验收 |
| OGG | CPU 流式 libvorbis 解码、PCM 环、音效混音；FPGA 输出 PCM | 100 MHz CPU 的持续解码/游戏负载实测 |
| 存档 | 五槽、软件提交与错误反馈；退出时 SD 回写 | 菜单 Quit 后重启读回、SD 持久化 |
| 单色显示模式 | APF `0x00B8`/灰度输出 RTL 已实现并通过行为仿真 | 新 bitstream 的 map/fit/asm/STA 与真机验收 |
| 构建交接 | 源码/依赖/驱动可迁移；阶段失败应保留日志 | 接收环境的工具链、镜像、缓存与许可准备 |

保持以下契约：

- 原 MiSTer 的 CMake、HPS 后端和共享游戏引擎不因 Pocket 构建而改写；新平台隔离在 `pocket/`。
- 不补成 320×224/240，不裁切或软件缩放。320×200 的原图并非方形像素 4:3；默认由 Pocket scaler 作 4:3 显示。
- LCD 模式按宽高独立整数缩放，可能不保留 4:3。可选 profile 必须显式选择并接受此差异；不能只改 JSON 就声称比例已验证。
- 实际 FPGA Vorbis 解码/offload **未实现**；PCM 播放器、混音器不等于 Vorbis 解码器。CPU 性能够不够也尚未证明。
- 保留 DOS/RNG 动画；本候选禁用 Windows AVI。OGG 不替换为预解码 PCM、MIDI 或 RIX 来假装达标。
- 存档关闭仅是运行时提交；保存后要用 Pocket 菜单 **Quit**，等回写结束再断电/拔卡。禁用睡眠，不承诺断电安全。

## 2. 统一包与离线范围

统一 ZIP 解压根目录为 `SDLPAL-Pocket-build-handoff/`：

```text
README_构建交接.md / BUILD_SPEC.md
source/                          项目构建源码快照，含 pocket/ 与原 MiSTer
dependencies/                    固定 Git bundles 与 musl-1.2.5.tar.gz
candidate-v1/game/                旧 SDK runtime 的游戏开发候选，不含游戏素材
candidate-v1/probe/               同一旧 runtime 的合成显示/OGG probe
evidence/                        本次交接检查记录
bootstrap_sources.py             仅本地恢复源码和 Git 依赖
handoff-manifest.json / SHA256SUMS
```

`bootstrap_sources.py --workspace ./work` 要求新的目标目录，恢复
`work/mister-pal-pocket` 和 `work/deps/{openfpgaSDK,openfpgaCore,VexiiRiscv,SpinalHDL,rvls}`，
并复制 `work/deps/musl-1.2.5.tar.gz`。部分依赖 bundle 另带 `.shallow` 边界文件，bootstrap
会自动恢复；仅手工 `git clone` bundle 不能代替这一完整流程。
它不下载或安装工具、不运行 Quartus、不写 SD 卡。项目源码直接恢复快照，不附原项目 Git 历史；
与 Pocket/MiSTer 构建无关的 Windows 签名密钥类文件被排除，基线身份和排除项在 manifest 中记录。
SDK/core/CPU 等五份 Git bundle 保留固定源码身份；原引擎及平台所需构建源码保留。

**离线恢复源码 ≠ 离线全量编译环境。** 包内不承诺包含 Docker 镜像、操作系统包、
GCC/JDK/sbt 可执行程序、sbt/Maven/Coursier 缓存、Quartus/Cyclone V 安装介质或许可证。
这些必须由接收者提前准备。Vexii 源码完整也不表示 sbt 的外部依赖已缓存。
旧候选里的 `.rbf_r` 只能作为旧 runtime 的候选使用，不能充当新 RTL 的编译结果。

## 3. 固定输入与环境门槛

### 源码身份

| 组件 | 固定身份 |
| --- | --- |
| MiSTer/PAL 基线 | `1d35d896b99722b9968685c1284cb5f1566cd662` |
| SDK | `a408ddc12aed0dfaa4aa22c06af82f829db77126` |
| 匹配 SDK 的 runtime 源码 | `618a3eb985759a4154115109c2c8036271252888` |
| VexiiRiscv | `580b76c3868512c8316bb7a3d3add81cad49a0dc` |
| SpinalHDL | `6f8510cdbb8ad7b8bcc0f6d58395669c4c4d7e2d` |
| rvls | `94f850e5f7c9a36e5aceb3810d658fffae55b497` |
| 显示补丁 | `pocket/fpga/display-modes.patch`；SHA-256 见 `pocket/fpga/source.json` |
| C 库 | SDK 固定的 musl；重建来源为 musl 1.2.5，加 core 自带 ilp32f 补丁 |

以 `pocket/pins.json`、`pocket/fpga/source.json`、`pocket/build-lock.json` 和交接 manifest 为核验依据。
`8318f25066b0b75d2e34081919dfc337ac6abf73` 只是早期研究参考，不能替代上述 runtime 基线。
拒绝用 `main`/`latest` 或上游 Makefile 的 fallback branch 顶替缺失 commit。
修改版本、补丁或工具链应产生新的构建记录，不能沿用旧哈希冒充重现。

### 接收环境

- 宿主至少有 Git、Python 3 和可用 Docker；宿主人工编译/测试另需 GNU Make、Bash、
  基础 GNU 工具。驱动仅支持 Linux/macOS 上的本地 Linux Docker daemon，非远程 daemon。
  源码/依赖/缓存/工作路径不得含空格或 shell/Make 特殊标点；这是上游规则与驱动的实际限制。
- 应用编译必须支持 `rv32imafc` / `ilp32f`，用 SDK 的 musl 头/库与 linker script。
  不可替换为 Linux glibc、host libc 或未经核对的 newlib 应用链接。
- 本次旧候选验证过 Debian GCC `14.2.0+19`、binutils `2.44-3+7+b1`，包校验值在
  `pins.json`。新的容器编译必须另外记录镜像 ID/digest 和实际工具版本；标签相同不等于内容相同。
- 新 FPGA 构建用 Quartus Prime **25.1** 和 **Cyclone V** device support，目标器件
  **5CEBA4F23C8**，变体 **os25**，维持原有 **100 MHz** 目标与时序约束。
  驱动要求 Quartus 镜像为 **Ubuntu 22.04 / linux/amd64**。
  只装 Quartus 主程序、不装 Cyclone V 器件数据不足以构建。
- CPU 网表生成需要 JDK 21/sbt 工具镜像以及与该依赖集合匹配的预热缓存；
  firmware 镜像要含 RISC-V 编译工具和 `hexdump`。缺 `hexdump` 曾让上游流程误产全 NOP MIF。
- Host 检查另需 C 编译器；OGG 测试/probe fixture 需 FFmpeg 的 libvorbis 编码器和
  `libvorbisfile.so.3`、`libvorbis.so.0`、`libogg.so.0`；RTL 检查需 Icarus Verilog/VVP。
  core 的额外 Verilator 回归另需其工具和依赖，不能因为 PAL 仿真通过就省略。
- 磁盘同时容纳源码、镜像、安装介质、生成网表、Quartus db/报告与每个 job 的副本。
  可先按 **至少 50 GiB 空闲磁盘、16 GiB RAM、一次一个 Quartus job** 做工程预算；
  这是保守规划建议，非厂商最低配置或成功保证。还要检查 Docker 的独立磁盘/RAM 配额。
  驱动的 FPGA 预检硬门槛是工作文件系统 **30 GiB 空闲构建空间**，不包含外部镜像/安装预算；
  驱动没有 RAM 保证，接手者仍须分配足够内存。
  缺空间或内存时先停下，不能通过删唯一源码/旧候选或降低 100 MHz 要求来“通过”。

接收者应按目标平台当前厂商支持列表准备完整环境。历史云环境的 Quartus 安装器
在运行前即 SIGSEGV，原因未查明；不能归因为已证实的 glibc/系统问题。
本交付不要求重试安装，也不附带商业工具。许可证、补充协议与容器镜像的再分发条件
由接收者按实际工具版本确认。优先使用合法准备的 Lite 环境；Standard/IP 若需授权必须具备。
驱动可用 `--license-file` 挂载已有本地授权，不会接受新协议；禁网构建不支持网络 license server。
`--quartus-root` 指已有安装中的 `quartus` 目录本身，不是 ALTERA 父目录，且 bind 模式仅支持 Linux。

## 4. 分阶段构建流程

统一入口为 `python3 pocket/tools/build_handoff.py --help`；逐阶段执行，失败即停。
完整命令见根交接 README。驱动与锁文件是可编辑源码；不要把仅显示命令的 `plan`
当作运行结果，也不要把 `preflight` 通过当作编译/时序通过。

| 阶段 | 必须完成的事 | 成功证据 |
| --- | --- | --- |
| 依赖/预检 | 核验本地 pins、补丁、工具/镜像、缓存、输入位置 | 版本、输入哈希和缺项记录 |
| prepare-core | 从精确 core 创建独立补丁树；恢复固定子依赖 | 补丁已应用；原 SDK/runtime 无修改 |
| game | 创建 PAL 隔离副本，实际编译并静态链接完整游戏引擎 | ELF；另外核验 ELF32/RISC-V/single-float、入口/链接图/符号 |
| probe | 创建/复用未改动 PAL 副本，编译独立 RV32 probe | probe ELF；合成 pal.pak 用独立脚本另行生成 |
| firmware | 重建 boot.bin、firmware.mif、os.bin | MIF 和 boot.bin 逐字节一致，非全 NOP |
| netlist | 从固定 Vexii/Spinal 输入生成 os25 网表 | 网表存在、记录 hash/config/工具版本 |
| fpga | 新 job 的 QSF；顺序 map → fit → asm → STA | `.sof/.rbf`、各阶段完整报告、返回码 |
| report | 读取已保存的阶段状态/产物 hash/镜像身份/失败记录 | 既有记录；不重跑检查、不自动宣告 release 合格 |

构建驱动使用预先存在的本地容器镜像；不应静默 pull、联网安装或调用上游自动烘焙镜像。
firmware/netlist/Quartus 阶段的独立工作副本和缓存必须足够；丢失缓存要先补齐，不能放宽 pins。
`prepare-core` 不需要 Docker 镜像，可先独立恢复新 core。`game/probe` 不依赖它。
probe 合成数据另跑 `python3 pocket/tools/make_probe_assets.py <本机输出pal.pak>`，
需要宿主 FFmpeg/libvorbis；该动作不是驱动的 `probe` 阶段，也不是打包或设备运行。

应用基础人工命令仍然有效，适合已有 RISC-V 工具链的开发机；这些是 **host-toolchain 路径**，
不是容器发行证明：

```sh
# 在项目源码根目录；SDK_ROOT 是本机 SDK 路径
make -C pocket pins SDK_ROOT="$SDK_ROOT"
make -C pocket SDK_ROOT="$SDK_ROOT" CROSS=riscv64-unknown-elf- -j4
SDK_ROOT="$SDK_ROOT" make -C pocket check SDK_ROOT="$SDK_ROOT"
CORE_ROOT="$CORE_ROOT" PAL_REQUIRE_RTL=1 \
  python3 -m unittest pocket.tests.test_display_modes -v
```

`SDK_ROOT` 传入 Make 的相对路径是相对 `pocket/`；使用已解析的本机绝对路径最稳妥。
更换编译器或 SDK 之前使用新的 build/obj 目录，不能混用旧对象。
统一驱动对 PAL 和准备好的 core 做源码指纹检查：编辑原源码后选新 `--work`；
不能修改 `work/pal` 或 `work/core` 后假定驱动会自动采纳。确需修改锁定 RTL/配置时，
把修改纳入源码 patch/lock，再创建新工作树。驱动不覆盖已存在的 FPGA job。
最终 ELF 可能按 SDK 脚本移除了 `.riscv.attributes`，所以还要检查对象 ISA 与实际编译命令。

### FPGA 的强制发布门槛

1. 先读匹配 core 的 `CLAUDE.md`。每个 job 独立 `bld/<job>/`、QSF、db、output_files，
   每个容器独立 HOME/TMPDIR。并发的裸机 Quartus 共享用户状态可能崩溃，单独目录不够。
2. 迁移后重新生成 QSF，绝不复用包含旧绝对路径的 QSF/db。
   记录 seed、器件、处理器数和实际生成的宏；不发明 Quartus 的 `--seed` 等参数。
   当前驱动固定 job `pal-handoff`、`QPROCS=4`；seed 由固定 core 的
   `src/fpga/targets/pocket/seeds/os25.seed` 读取，不是历史准备目录的手工 seed。
3. 在 map 前用 `pocket/fpga/verify_boot_image.py` 检查新树中
   `src/firmware/os/bld/pocket/{firmware.mif,boot.bin}`；firmware make 返回 0 不足以代替它。
4. 必须保存并检查 map/fit/asm/STA 全日志、资源用量、全部 relevant setup/hold、
   时钟与 CDC/未约束路径。已有 os25 接近 M10K 上限；以上游注释或旧报告猜剩余资源无效。
5. 不允许负 setup/hold slack、漏时钟约束或未解释的严重警告进入“时序通过”。
   只看到成功返回码、Fmax 或一个最坏 setup 值，仍不能签核全部路径。
6. 新增灰度输出不应破坏 blanking 中的 APF metadata；确认 RGB 与 DE/HS/VS/SKIP 对齐，
   APF ack 在实际应用后返回；超时、取消、回滚与 warm reset 回归必须继续通过。
7. 上游 `USE_QUARTUS_CONTAINER=0` 不能把 `make build/full` 变成完整原生流程。
   如自行作裸机诊断，必须单独标识；不得冒充要求的隔离容器发行构建。

## 5. 新 runtime 与打包：故意留出的人工审查关卡

`pocket/tools/package.py` 只接受原 pin 的完整 SDK/runtime。它会拒绝脏 SDK、错误
manifest/checksum 和已有输出目录。**不要删检查，也不要把新 `.rbf_r` 塞进旧 manifest。**
统一驱动生成新 FPGA 产物，并不自动更新 SDK、打包单色发行版或写入 SD 卡。

要形成新 runtime 候选，接手者必须：

1. 在独立的新 SDK staging 树中，同步同一次构建的 ABI/header、musl、boot 对应
   bitstream、os.bin 与所需 loader；检查 core 的 `sdk-runtime.sh`/`make sdk` 路径。
   SDK 导出可能读取该树残存的其它产物，不能仅运行命令便认定全部文件都是新版本。
2. 记录新 source+patch 身份、镜像 ID、工具版本、variant/seed、生成输入及产物 SHA-256；
   创建一致的 runtime manifest。保留原 pin/SDK 和旧候选以供回退。
3. 有意修改并审查新 pin、打包策略与其拒绝混装测试。当前脚本没有“忽略校验”的发行捷径。
4. 显式选 `pocket/fpga/profiles/` 的一个 profile；默认继续 normal/CRT。
   `color-v1.video.json` 无单色 ID，可用于旧 runtime 的自愿色彩 LCD 试验。
   `generic`、`nintendo-sega`、`snk-nec-atari` 含单色 ID，必须用新建且验证的 runtime。
   每个 profile 不超过 16 项，合计覆盖的 22 个 ID 不表示单一 JSON 可塞入全部模式。
5. 先制作独立 probe 候选，记录包 hash，完成真机显示/切换验证；再做游戏候选与完整验收。
   在完成前一律标为开发候选，不得改名“稳定版”或“全部滤镜已完成”。

## 6. 修改代码时去哪里

| 想改什么 | 主要代码/配置 | 最先回归 |
| --- | --- | --- |
| 原始游戏逻辑 | `sdlpal/` | 共享引擎/MiSTer 回归；优先用平台边界解决差异 |
| 显示、调色板、pitch/stride | `pocket/video.c`、`video_pixels.h`、`config/video.json` | `test_video.py`、probe 边框/棋盘/比例 |
| 按键与重复/边沿 | `pocket/input.c`、`input_logic.h`、`config/input.json` | `test_input.py` |
| OGG、PCM 时序/混音 | `pocket/audio.c`、`audio_stream.c/.h` | `tests/audio_test.py`；真机诊断 |
| 资源包、保存、错误提示 | `pocket/files.c/.h`、`platform.c`、`tools/pack_assets.py` | `test_pack_assets.py`、`test_platform.py` |
| APF 单色协议和输出 | `pocket/fpga/display-modes.patch` 对应 core 的 bridge/top/新 video stage | `test_display_modes.py`；map/fit/STA |
| CPU/cache/资源配置 | 新 core 的 `src/fpga/vendor/vexriscv/configs/os25.cfg` | 强制新网表、新 job、性能/时序重测 |
| 环境/构建与发货校验 | `pocket/tools/build_handoff.py`、`check_sdk.py`、`package.py`、pins | 构建驱动测试和 `test_package.py` |

改 RTL 后同时更新补丁、`source.json` 与 `build-lock.json` 校验身份；改 CPU 配置后不能继续用旧生成网表。
这类变更都要重新编译验证，不能仅修改报告里的版本号。

## 7. 日志、失败与接手完成标准

统一驱动的工作目录保存 `handoff-state.json` 和成功预检的 `preflight.json`；日志为
`logs/<timestamp>-<stage>-<sequence>.log`，命令索引为 `logs/commands.jsonl`。
`plan` 不写文件或执行命令；`preflight` 可写预检 JSON/日志，但不修改源码或构建产物。
用 `preflight --stage game` 等做分阶段检查，避免仅编译游戏时也被缺 Quartus 阻塞。
Quartus 原始证据在新 core 的
`src/fpga/targets/pocket/bld/pal-handoff/output_files/`，应用产物在隔离 PAL 副本的
`pocket/build/{game,probe}/`，boot/OS 产物在 core 的 `src/firmware/os/bld/pocket/`。
驱动实际生成的路径及日志名以其 `plan/report` 输出为准，不迁移旧绝对路径。
新 `os25.rbf_r` 同样保留在该 job 的 output_files 中；驱动不重建 loader，也不导出 runtime manifest。

- `pin mismatch`：检查 bootstrap、commit 和补丁是否匹配；不要强行换分支。
- 缺 Docker/镜像/缓存/Quartus device：是接收环境未就绪；不视为 RTL 或游戏测试失败。
- 全 NOP MIF：检查 `hexdump` 和生成步骤，重新生成再逐字节验证。
- map/fit/STA 错误：保存完整日志，确认实际 QSF 来源、器件、宏、seed、时钟与资源；
  修复后用新 job 重跑受影响阶段，不能拿历史 RBF 代替。
- 可链接但开机黑屏：检查 runtime 一致性、服务 ABI/存储布局、boot contract 和 assets；
  不能先把问题归因于游戏性能。
- OGG 欠载：先取真机 decode time/pump gap/队列数据，再优化。若确实 CPU 不够，
  真 FPGA Vorbis offload 要另建 RTL/传输/黄金 PCM/资源时序/真机验证工作，当前没有这个实现。

主机测试覆盖输入、显示、资源边界、存档故障、音频流和打包保护；RTL 行为测试覆盖
APF/灰度/CDC 协议。它们不能证明真机启动、音频实时性、SD 持久化、滤镜外观或物理时序。
历史 ASan 检查禁用了 LeakSanitizer；vendored libogg/vorbis 的 signed-shift UBSan
诊断仍存在，不能写“全部 sanitizer 清洁”。

达到“接手构建完成”至少要有固定输入、新 ELF、正确 boot/netlist、新 FPGA 全报告和
一致 runtime 候选；达到“可放心游戏”还要完成验收单的设备项。
游戏数据由使用者合法提供，制作私有 `pal.pak`；SD 写入、设备测试和最终发布均由接手者有意执行。
公开再分发前保留 GPL 对应源码与构建脚本、全部第三方 notices，核对依赖及二进制授权；
不带仙剑素材、商业音乐、样例音色库、Quartus 安装器或私有许可证。
