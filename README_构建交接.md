> Public repository adaptation (2026-10-08): this document describes the original build handoff. Before following its commands, run reconstruct_inputs.py and see DEPENDENCIES.md. SDK/Core/SpinalHDL bundles and old runtime candidates are not redistributed here; bootstrap requires explicit --sdk, --core and --spinal local checkout paths. No hardware-ready runtime is included.

# SDLPAL Pocket：从这里继续编译

这份交接包带完整引擎/平台构建源码、精确依赖源码、构建驱动和旧 v1 候选。
源码快照不附原项目 Git 历史，排除无关 Windows 签名密钥类文件；基线与排除清单在 manifest。
**单色 RTL 已实现并仿真，但新 FPGA bitstream 尚未编成；真机和 OGG 性能尚未验证。**
默认仍是原生 320×200、4:3 normal/CRT。LCD 整数缩放可能改变比例，须自愿选择。

详细规范：[BUILD_SPEC.md](BUILD_SPEC.md)。验收单在源码树的
`pocket/docs/build-handoff-acceptance.md`。原 `README.md` 继续说明 MiSTer。

## 1. 离线恢复源码

在解压后的 `SDLPAL-Pocket-build-handoff/` 根目录运行：

```sh
sha256sum -c SHA256SUMS
python3 bootstrap_sources.py --verify-only
python3 bootstrap_sources.py --workspace ./work --sdk /path/to/openfpgaSDK --core /path/to/openfpgaCore --spinal /path/to/SpinalHDL
cd work/mister-pal-pocket
```

`work` 必须是新目录；恢复日志在 `work/logs/bootstrap.log`。
恢复过程只读本地 bundle，不联网、不安装工具。它会自动还原依赖的 `.shallow` 边界文件，
并把 musl 源码包放入 `work/deps/`；不要只手工 clone bundle 而漏掉这些步骤。
`dependencies/` 是源码，不包含一套可直接运行的 Quartus/Docker/GCC/sbt 环境。

## 2. 指定本机输入，先看计划

已准备好 Git、Python 3、Docker 和所需本地工具镜像后，在恢复的源码根目录运行。
源码、依赖、缓存和工作目录均不要有空格或 shell/Make 特殊标点；bootstrap 能接受的
路径不一定能被上游构建规则接受。驱动支持 Linux/macOS 上的本地 Linux Docker daemon。

```sh
BUILD_WORK=../build
pb() {
  python3 pocket/tools/build_handoff.py "$@" \
    --sdk ../deps/openfpgaSDK --core ../deps/openfpgaCore \
    --vexii ../deps/VexiiRiscv --spinal ../deps/SpinalHDL --rvls ../deps/rvls \
    --musl-archive ../deps/musl-1.2.5.tar.gz \
    --work "$BUILD_WORK"
}
pb plan
```

`plan` 只显示计划，不编译。默认镜像名为 `openfpgaos-firmware`、`openfpgaos-vexii`、
`openfpgaos-quartus-full`；它们必须已存在于本机。可通过对应 `--*-image` 参数选择
自己的已备镜像。驱动不自动 pull、不运行安装器；镜像标签不是工具版本证明。

## 3. 先构建真实游戏 ELF 与合成 probe

```sh
pb preflight --stage game && pb game
pb preflight --stage probe && pb probe
pb report
```

game/probe 不要求先安装 Quartus。输出分别在：

- `../build/pal/pocket/build/game/app.elf`
- `../build/pal/pocket/build/probe/app.elf`

probe 阶段只编译 ELF；需要新合成数据时，使用宿主已安装的 FFmpeg/libvorbis：

```sh
python3 pocket/tools/make_probe_assets.py ../build/pal/pocket/build/probe/pal.pak
```

probe 是无商业素材的测试程序，不能代替完整游戏性能验收。
主机/OGG/RTL 测试的工具要求和命令见 BUILD_SPEC；ELF 成功链接不代表这些测试已经运行。

## 4. 接着构建新 FPGA runtime

先备好 Quartus 25.1 + Cyclone V 支持、os25 生成所需的 JDK/sbt 镜像及完整预热缓存。
缓存位置用本机实际目录设置，不要指向空文件夹：

```sh
# 先设置 VEXII_CACHE 为你已有的完整 sbt/Maven/Coursier 缓存目录
test -n "${VEXII_CACHE:-}" && test -d "$VEXII_CACHE" && \
  pb preflight --stage all --vexii-cache "$VEXII_CACHE" && \
  pb prepare-core && pb firmware && \
  pb netlist --vexii-cache "$VEXII_CACHE" && pb fpga
pb report
```

遇到非零返回码先停下，不要继续把旧产物当新结果。阶段日志在
`../build/logs/`，命令记录为 `../build/logs/commands.jsonl`，
最近成功的预检结果为 `../build/preflight.json`，状态为 `../build/handoff-state.json`。
Quartus 原始报告在 `../build/core/src/fpga/targets/pocket/bld/pal-handoff/output_files/`。
驱动在 FPGA 前要求工作文件系统至少有 30 GiB 空闲空间；全套环境建议预留更多。
运行完成后仍要人工检查完整 setup/hold、约束、资源和警告。

驱动不自动把新 bitstream 塞进旧 SDK，不自动产生单色发行包。
新 runtime 的一致 manifest/pin 更新、profile 选择和候选打包审查见 BUILD_SPEC 第 5 节。

## 5. 继续改代码与设备测试

- 在 `work/mister-pal-pocket/pocket/` 修改平台代码；完整原引擎在 `sdlpal/`。
  构建使用隔离副本，不能直接改 `build/pal`。修改原源码后选新的 `BUILD_WORK`，
  避免编译旧副本/对象；同一 FPGA job 已存在时也要新建工作目录。
- FPGA 修改入口是 `pocket/fpga/display-modes.patch` 和匹配 core 的 RTL；改后更新校验身份、
  仿真并重新 map/fit/asm/STA。不要降低 100 MHz 目标来掩盖时序失败。
- `candidate-v1/{game,probe}/` 是单独保留的旧 SDK 候选，仍未真机验证。
  不含新单色 bitstream；不得与新 os.bin、SDK 或单色 profile 混装。
- 游戏数据与 OGG 由你合法提供，制作私有 `pal.pak`。不将正版素材纳入公开包。
- SD 卡写入、Pocket/Dock 试机和发布由你有意执行。保存后用 Pocket 菜单 Quit，等回写完成再关机。

OGG 当前是在 RISC-V CPU 上真实解码，由 FPGA 输出 PCM；真正 FPGA Vorbis offload 尚未实现。
完成构建后按验收单记录显示、音乐负载、五槽保存/退出/重启，不把“能编译”写成“已可放心通关”。
