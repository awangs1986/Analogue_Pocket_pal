# SDLPAL for Analogue Pocket（仙剑奇侠传 Pocket 移植）

把开源引擎 [SDLPAL](https://github.com/sdlpal/sdlpal) 移植到 Analogue Pocket：C 引擎跑在 openfpgaOS 的
VexiiRiscv RISC-V CPU 上（`rv32imafc`/`ilp32f`），FPGA 负责显示和 PCM 音频。

> **状态：测试版，未经硬件完整验证。** 这不是正式发布，也不提供可直接刷写的 bitstream。
> 真机测试已经发现并修复了一个问题：核心目录缺 `variants.json` 时 Pocket 会报 “load error in variants”，
> 现在打包脚本会自动带上它。其他功能（存档、Dock、显示模式等）还需要按测试清单逐项验证。

- **两个版本**：DOS 版（繁体，`PAL_DOS/`）和 **PAL98 版（推荐：简体中文 + 开场 AVI 动画，`PAL98/`）**，见 [PAL98 版](#pal98-版简体中文--开场动画推荐)。
- 原生 320×200 8 位索引色画面，Pocket 缩放器按 4:3 显示。
- Ogg/Vorbis 音乐在 RISC-V CPU 上解码，48 kHz 立体声 PCM 输出（不是 FPGA 解码）。
- 所有游戏资源打包成一个只读的 `pal.pak`；5 个存档位映射到 Pocket 的非易失存档。
- **仓库里没有任何游戏数据、音乐或 bitstream。** 需要你自己购买正版，自己生成 `pal.pak`、自己编译核心。

## 目录

1. [需要准备什么](#1-需要准备什么)
2. [从 Steam 提取游戏文件](#2-从-steam-提取游戏文件)
3. [音乐：订阅 Steam 创意工坊](#3-音乐订阅-steam-创意工坊)
4. [制作 pal.pak](#4-制作-palpak)
5. [安装到 SD 卡](#5-安装到-sd-卡)
6. [从源码构建](#6-从源码构建)
7. [存档](#7-存档)
8. [已知限制](#8-已知限制)
- [PAL98 版（简体中文 + 开场动画，推荐）](#pal98-版简体中文--开场动画推荐)
9. [卸载](#9-卸载)
10. [版权说明](#10-版权说明)

## 1. 需要准备什么

- Analogue Pocket，固件 **2.2 或更高**（`core.json` 的 `version_required` 是 2.2）。
- 一张 SD 卡（建议先完整备份，尤其是 `Saves/`）。
- Steam 正版《仙剑奇侠传》（**Steam app 1546570**），安装在本机。
- 电脑上有 Python 3（用来运行打包脚本）。
- 核心 bitstream：仓库不提供，需要按第 6 节自己编译。

## 2. 从 Steam 提取游戏文件

Steam 版安装目录下有三个子目录：`PAL/`、`PAL98/`、`PAL_DOS/`。本节说的是 **DOS 版**，只用 `PAL_DOS/`（繁体中文，Big5 编码）。
想要简体中文和开场动画，请看 [PAL98 版](#pal98-版简体中文--开场动画推荐)，那个版本用 `PAL98/`。

常见安装路径：

| 系统 | 路径 |
|---|---|
| Windows | `C:\Program Files (x86)\Steam\steamapps\common\PAL\PAL_DOS` |
| Linux | `~/.local/share/Steam/steamapps/common/PAL/PAL_DOS` |

如果 Steam 库在别的磁盘，就换成对应的 `<Steam 库>\steamapps\common\PAL\PAL_DOS`。可以在 Steam 里右键游戏 →“管理”→“浏览本地文件”打开。

需要的文件（**18 个**）：

| 类别 | 文件 |
|---|---|
| 13 个 MKF 资源 | `abc.mkf` `ball.mkf` `data.mkf` `f.mkf` `fbp.mkf` `fire.mkf` `gop.mkf` `map.mkf` `mgo.mkf` `pat.mkf` `rgm.mkf` `rng.mkf` `sss.mkf` |
| 音效 | `voc.mkf` |
| 文本和字库 | `m.msg` `word.dat` `wor16.asc` `wor16.fon` |

- Steam 里的文件名是大写的（如 `ABC.MKF`），复制时要**转成小写**。下面的脚本会自动处理。
- **不需要**：`.exe`、声卡驱动和配置文件、`.rpg` 存档、CD 镜像、`MIDI.MKF`、`MUS.MKF`。本移植的音乐用 OGG，不用 MIDI/RIX。
- DOS 版不用 `PAL/` 和 `PAL98/`：
  - `PAL98/` 是 Windows 95/98 版，资源和 DOS 版不同，要用单独的 PAL98 核心和 `--edition pal98` 打包；
  - `PAL/` 是 Steam 新版的程序目录。

## 3. 音乐：订阅 Steam 创意工坊

仙剑 DOS 版的音乐是 MIDI/RIX，本移植只播放 OGG，所以需要创意工坊的 OGG 音乐包。两种都可以用，**推荐 OGG 编曲版**。

| 选项 | 工坊条目 | 曲目 | 脚本参数 |
|---|---|---|---|
| **OGG 编曲版（推荐）** | [SDLPal Arranged Soundtracks（2449282849）](https://steamcommunity.com/sharedfiles/filedetails/?id=2449282849) | 01–87，共 87 首 | `--music arranged` |
| 原声版 | [PAL SC Soundtracks（2433259482）](https://steamcommunity.com/sharedfiles/filedetails/?id=2433259482) | 86 首，缺第 29 首 | `--music sc` |

两个音乐包的作者都是 abalonelife（孙志贵）：
- 编曲版是为 SDLPal 重新编曲、混音的版本；
- 原声版是用 Roland VSC-88 重新录制的原版 MIDI。MIDI 版本身就没有第 29 首，所以缺 29 是正常的。

订阅方法：
1. 用购买了仙剑的 Steam 账号登录，打开上面的工坊链接，点 **“订阅”**。
2. 启动一次 Steam（或在“库”里让游戏更新），等待下载完成。
3. 订阅后文件会下载到 Steam 库的这个目录（目录名就是工坊条目 ID）：
   - Windows：`C:\Program Files (x86)\Steam\steamapps\workshop\content\1546570\2449282849\`
   - Linux：`~/.local/share/Steam/steamapps/workshop/content/1546570/2449282849/`（原声版把 ID 换成 `2433259482`）
4. 目录里的 `.ogg` 文件按曲目编号命名，可能在子目录里，可能是两位或三位数。脚本会递归查找、检查曲目是否齐全，然后统一复制成 stage 目录里的 `ogg/NN.ogg`（`01.ogg` … `87.ogg`）。引擎按 `ogg/%02d.ogg` 读取音乐。

## 4. 制作 pal.pak

推荐用一键脚本 `source/pocket/tools/make_pal_pak_from_steam.py`。它会：
- 从 Steam 目录**只复制**需要的文件，不改动 Steam 文件；
- 把文件名转成小写，检查 18 个数据文件和全部曲目是否齐全、OGG 文件头是否正确；
- 生成 `stage/` 和 `pal.pak`，并做 `--verify` 校验；
- 已有的 `stage/` 或 `pal.pak` 不会被覆盖。

在**仓库根目录**运行：

```sh
# OGG 编曲版（推荐，87 首）
python3 source/pocket/tools/make_pal_pak_from_steam.py \
  --pal-dir ~/.local/share/Steam/steamapps/common/PAL \
  --music arranged \
  --out ./my-pal-arranged

# 原声版（86 首，缺 29）
python3 source/pocket/tools/make_pal_pak_from_steam.py \
  --pal-dir ~/.local/share/Steam/steamapps/common/PAL \
  --music sc \
  --out ./my-pal-sc
```

- Windows 下把 `--pal-dir` 换成 `"C:\Program Files (x86)\Steam\steamapps\common\PAL"`。
- 脚本默认在同一个 Steam 库的 `steamapps/workshop/content/1546570/<ID>` 里找音乐。如果工坊目录在别处，用 `--music-dir` 指定。
- 输出是 `<out>/pal.pak`。**不要把它放进仓库，也不要分享给别人。**

也可以手动制作：
1. 建一个 stage 目录，放入上面 18 个小写文件名的数据文件，以及 `ogg/01.ogg`、`ogg/02.ogg`……
2. 运行：

```sh
python3 source/pocket/tools/pack_assets.py /path/to/stage /path/to/pal.pak
python3 source/pocket/tools/pack_assets.py --verify /path/to/pal.pak
```

如果已经用 bootstrap 生成了 `work/mister-pal-pocket`，在那个目录里对应的路径是 `pocket/tools/...`。格式说明见 [assets-saves.md](source/pocket/docs/assets-saves.md)。

## 5. 安装到 SD 卡

有两套核心，ID 和平台都互不相同，可以同时安装，存档也是分开的：

| | 正式核心（开发候选） | tc6 时序收敛测试核心 |
|---|---|---|
| 核心 ID | `awangs1986.PAL` | `awangs1986.PALtc6` |
| 平台 ID | `sdlpal` | `sdlpaltc6` |
| 生成脚本 | `source/pocket/tools/package.py` | `source/pocket/tools/package_tc6.py` |

SD 卡目录结构（以正式核心为例；tc6 核心把 `sdlpal` 换成 `sdlpaltc6`，`awangs1986.PAL` 换成 `awangs1986.PALtc6`）：

```text
SD 卡根目录/
├── Cores/awangs1986.PAL/
│   ├── core.json  audio.json  data.json  input.json  video.json  interact.json  variants.json
│   ├── os25.rbf_r        ← 自己编译的 bitstream
│   └── loader.bin
├── Platforms/sdlpal.json
└── Assets/sdlpal/
    ├── common/
    │   ├── os.bin  pal.elf  pal.ini
    │   └── pal.pak       ← 你自己生成的游戏数据
    └── awangs1986.PAL/PAL.json
```

关键是：**`pal.pak` 放在 `Assets/<平台 ID>/common/pal.pak`**，正式核心是 `Assets/sdlpal/common/pal.pak`，tc6 核心是 `Assets/sdlpaltc6/common/pal.pak`。

生成核心目录（两套都不含游戏数据，也都需要先自己编译出 bitstream 和 ELF）：

```sh
# 正式核心：使用固定版本 SDK 中的 os25.rbf_r（见 pins.json）
python3 source/pocket/tools/package.py --sdk /path/to/openfpgaSDK --elf /path/to/app.elf --out /path/to/new-dir

# tc6 测试核心：使用自己用 timing-closure-tc6 补丁编译的 bitstream
python3 source/pocket/tools/package_tc6.py --tc-core /path/to/tc6-core --out /path/to/tc6-s16-sd
```

把生成目录里的 `Cores/`、`Platforms/`、`Assets/` 复制到 SD 卡根目录，再放入 `pal.pak`，然后安全弹出 SD 卡。
真机测试步骤见 [tc6-s16-test-checklist.md](source/pocket/docs/tc6-s16-test-checklist.md)。

## 6. 从源码构建

详细说明见 [BUILD_SPEC.md](BUILD_SPEC.md) 和 [构建交接文档](README_构建交接.md)。简要步骤如下：

1. **恢复输入**：`python3 reconstruct_inputs.py`，然后 `python3 bootstrap_sources.py --verify-only`。
2. **准备依赖**：openfpgaSDK、openfpgaCore、SpinalHDL 需要按固定 commit 自己 clone。它们的上游历史含有不可再分发的素材，所以仓库里不提供，见 [DEPENDENCIES.md](DEPENDENCIES.md)。然后运行：
   ```sh
   python3 bootstrap_sources.py --workspace ./work --sdk /path/to/openfpgaSDK \
     --core /path/to/openfpgaCore --spinal /path/to/SpinalHDL
   ```
3. **pb 构建驱动**：在 `work/mister-pal-pocket` 里用 `pocket/tools/build_handoff.py`，依次运行：
   - `plan`
   - `preflight`
   - `game` / `probe`（游戏 ELF）
   - `prepare-core`
   - `firmware`
   - `netlist`
   - `fpga`
   - `report`
4. **Docker 镜像**：需要本机已有 `openfpgaos-firmware`、`openfpgaos-vexii`、`openfpgaos-quartus-full`。驱动不会自动 pull 镜像，也不会安装任何东西。
5. **Quartus**：Quartus Prime **25.1 Lite** 加 Cyclone V 器件支持，目标器件 5CEBA4F23C8，时钟 100 MHz。
6. **时序收敛补丁（可选）**：[source/pocket/fpga/timing-closure-tc6](source/pocket/fpga/timing-closure-tc6) 是 4 个补丁加 seed 16，可以重放出四个工艺角全正的 tc6-s16 构建。它是独立步骤，没有接入 pb。详见 [中文时序报告](source/pocket/docs/timing-closure-tc6.md)。

## 7. 存档

- 引擎的 `1.rpg`…`5.rpg` 对应 Pocket 存档 `PAL_1.sav`…`PAL_5.sav`；设置保存在 `PAL.cfg`。
- **关机前要从 Pocket 菜单选 Quit 退出**，存档才会写入 SD 卡。不支持睡眠。
- DOS 或 Windows 版的存档不保证兼容。详见 [assets-saves.md](source/pocket/docs/assets-saves.md)。

## 8. 已知限制

- 测试版，**没有完成真机验收**（显示模式、Dock、存档持久化、长时间运行等都还要测）。
- AVI 动画只在 PAL98 核心里支持（MS Video1，RISC-V 软件解码）；不支持 MIDI/RIX/MP3/Opus 音乐，只支持 OGG。
- tc6 测试核心的已知问题：
  - GPU 是空壳，只适用于 PAL；
  - build_id 为 0；
  - x_count 跨时钟域的 hold 余量很小，需要 RTL 修复；
  - 时序结果对 seed 敏感。

## PAL98 版（简体中文 + 开场动画，推荐）

PAL98 是 Steam 里 `PAL98/` 目录的 Windows 95/98 版数据。和 DOS 版相比：

- **简体中文**：用 Steam 自带的 `MSG_chs.txt` / `WORD_chs.txt` 转换成 SDLPAL 的 UTF-8 文本，
  配置里自动选内置简体字库（`FontFlavor=SimpChin`）。需要繁体就加 `--lang cht`。
- **开场 AVI 动画**（`1.avi`…`6.avi`，MS Video1 + PCM）：播放时切到 RGB555 画面，音频重采样到 48 kHz，
  SD 卡读取用异步预读。
- **音效**用 Win95 版的 `sounds.mkf`（WAV，比 DOS 的 VOC 音质好），开机后在后台预读到内存，
  第一次播放不再卡顿；重采样用定点 16 抽头多相 FIR（系数表在编译时生成，见 `source/pocket/tools/gen_resample_coef.c`）。
- **音乐**同样用创意工坊的 OGG（推荐编曲版 2449282849）；PAL98 的 CD 音轨 `ogg/100NN.ogg` 默认一起打包。
  循环播放不重开解码器，没有接缝停顿。

**真机测试结果（tc6-s16 测试核心 + PAL98 简体包）**：开场 1/2/3.avi 流畅、音画同步，游戏内音效第一次播放不卡、
音乐循环无停顿，简体中文显示正常。存档、Dock 等其余项目仍按测试清单验证。

### 制作 PAL98 的 pal.pak

```sh
python3 source/pocket/tools/make_pal_pak_from_steam.py \
  --pal-dir ~/.local/share/Steam/steamapps/common/PAL \
  --edition pal98 \
  --music arranged \
  --out ./my-pal98-chs
# 默认 --lang chs（简体）；繁体用 --lang cht；不要 CD 音轨用 --no-cd-tracks
```

脚本会从 `PAL98/` 复制 MKF、`sounds.mkf`、6 个 AVI，把 `MSG_chs.txt`/`WORD_chs.txt` 转成 `pal98_chs.msg`，
写入 `sdlpal.cfg`（`MessageFileName=pal98_chs.msg`），再打包成 `<out>/pal.pak`（约 330 MB）。

### PAL98 测试核心

PAL98 用独立的核心 ID 和平台 ID，和 DOS 版可以同时安装、存档互不影响：

| | PAL98 测试核心 |
|---|---|
| 核心 ID | `awangs1986.PAL98tc6` |
| 平台 ID | `sdlpal98tc6` |
| pal.pak 位置 | `Assets/sdlpal98tc6/common/pal.pak` |

```sh
# 1) 编译游戏 ELF（默认就是发布配置：AVI 开、快速 AVI 路径开）
make -C source/pocket SDK_ROOT=/path/to/openfpgaSDK      # 输出 build/app.elf
# 2) 打包核心目录（bitstream 用 tc6-s16 构建）
python3 source/pocket/tools/package_tc6.py --edition pal98 --tc-core /path/to/tc6-core \
  --elf source/pocket/build/app.elf --out /path/to/pal98-sd
```

把 `Cores/`、`Platforms/`、`Assets/` 复制到 SD 卡根目录，再把 PAL98 的 `pal.pak` 放到 `Assets/sdlpal98tc6/common/`。

### 调试日志

PAL98 核心会把启动和运行日志写到存档槽 `PAL_debug.log`（`Saves/sdlpal98tc6/...`，最多 240 KB）：
启动各阶段、文件打开、AVI 每 30 帧的读取/解码耗时、音乐循环、音效首次播放、超过 150 ms 的脚本指令和
超过 400 ms 没有刷新画面的卡顿。只在出现慢操作时才写，正常运行几乎没有开销。遇到问题请附上这个文件。
屏幕上不显示日志（编译时加 `EXTRA_CFLAGS="-DPAL_POCKET_AVI_FAST -DPAL_POCKET_LOG_SCREEN"` 才会在启动时显示）。

## 9. 卸载

删除 SD 卡上的这些目录：
- 正式核心：`Cores/awangs1986.PAL/`、`Platforms/sdlpal.json`、`Assets/sdlpal/`；
- tc6 核心：`Cores/awangs1986.PALtc6/`、`Platforms/sdlpaltc6.json`、`Assets/sdlpaltc6/`；
- PAL98 核心：`Cores/awangs1986.PAL98tc6/`、`Platforms/sdlpal98tc6.json`、`Assets/sdlpal98tc6/`。

不再需要存档的话，再删 `Saves/` 下对应平台的目录（先备份）。

## 10. 版权说明

- 本仓库**不包含任何游戏数据**。《仙剑奇侠传》版权归大宇资讯（Softstar）所有，请使用自己购买的正版。
- 创意工坊音乐包的版权归原作者（abalonelife / 孙志贵）所有。请通过 Steam 订阅获取，不要转载或再分发。
- 自己生成的 `pal.pak` 只供个人使用，不要上传或分享。
- SDLPAL 使用 GPL-3.0。其他组件的许可见 [source/LICENSE.md](source/LICENSE.md)、[SOURCE_PROVENANCE.md](SOURCE_PROVENANCE.md) 和 [依赖说明](DEPENDENCIES.md)。

---

## English summary

SDLPAL port for Analogue Pocket (RISC-V engine on openfpgaOS). **Test build, not hardware-validated**, and there is no ready-to-flash release.

- **Game data:** you need your own Steam copy of PAL (app 1546570). Use only `PAL_DOS/`: 13 MKF files (`abc ball data f fbp fire gop map mgo pat rgm rng sss`), `voc.mkf`, `m.msg`, `word.dat`, `wor16.asc` and `wor16.fon`, with lowercase names.
- **Music:** subscribe to Workshop [2449282849 SDLPal Arranged Soundtracks](https://steamcommunity.com/sharedfiles/filedetails/?id=2449282849) (87 tracks, recommended) or [2433259482 PAL SC Soundtracks](https://steamcommunity.com/sharedfiles/filedetails/?id=2433259482) (86 tracks, no 29).
- **Build pal.pak:** run `python3 source/pocket/tools/make_pal_pak_from_steam.py --pal-dir <steamapps/common/PAL> --music arranged --out <new dir>` and put the result in `Assets/sdlpal/common/pal.pak`.
- **PAL98 edition (recommended):** `--edition pal98` builds a Simplified Chinese pak (default `--lang chs`) from `PAL98/` with the intro AVIs; use the `awangs1986.PAL98tc6` test core (`package_tc6.py --edition pal98`). Hardware-tested: smooth AVI, no first-SFX or music-loop hitches. A debug log is written to the `PAL_debug.log` save slot.
- **Core:** build the bitstream yourself, see [BUILD_SPEC.md](BUILD_SPEC.md) and [README_构建交接.md](README_构建交接.md).
- **Not included:** game assets, music, credentials and bitstreams.
- See [provenance](SOURCE_PROVENANCE.md), [input manifest](handoff-manifest.json), [stored-parts inventory](input-parts.json), [dependencies](DEPENDENCIES.md) and [licenses](source/LICENSE.md).
