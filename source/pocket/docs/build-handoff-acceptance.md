# 构建交接与真机验收记录

本表配合根目录 `BUILD_SPEC.md`。`[ ]` 表示还没有此次构建的证据；不得把历史
测试通过复制成新构建或真机通过。允许记“未运行/被阻塞”，必须写原因和证据位置。

## A. 身份与环境

```text
记录人 / 日期：
交接 ZIP SHA-256：
source snapshot / 基线 / 本地修改：
SDK / core / Vexii / SpinalHDL / rvls commit：
显示 patch SHA-256：
musl archive SHA-256 / ilp32f patch：
宿主 OS/架构 / Docker 版本：
firmware / vexii / Quartus 镜像 ID/digest：
GCC / binutils / JDK / sbt / Quartus 版本：
器件 / variant / seed / job / processor count：
工作目录 / 日志目录：
初始可用磁盘 / RAM / 容器配额：
```

- [ ] `SHA256SUMS` 校验成功；bootstrap 在新目录离线恢复源码，无安装/下载动作。
- [ ] SDK/runtime 原始 pin、manifest 和文件校验全部匹配；原目录未被改写。
- [ ] 使用 runtime `618a3eb…`，没有误用研究参考 `8318f250…` 或 fallback branch。
- [ ] 所需子依赖存在且与锁定 commit 一致；不依赖原作者绝对路径。
- [ ] Quartus 25.1 含 Cyclone V 支持，目标为 `5CEBA4F23C8`；所用工具授权已由接手者确认。
- [ ] Docker、镜像、sbt 缓存、工具和磁盘满足所选阶段；缺项明确记为阻塞。
- [ ] 输出/缓存/QSF 重新生成，不从旧 db、网表或候选 RBF 推断新结果。

## B. 软件与 probe

- [ ] `game` 实际编译全引擎并链接，不只是语法检查、空入口或 probe。
- [ ] ELF32、little-endian、RISC-V、single-float ABI；无动态解释器、无未解析符号。
- [ ] 对象与命令确认 `rv32imafc/ilp32f`；记录 ELF 入口、size、map、SHA-256。
- [ ] ELF 使用 SDK musl/header/linker script，没有混入 host/glibc/newlib 应用 ABI。
- [ ] host 输入、显示、文件/存档、错误 UI、打包与 OGG/音效检查成功；记录实际通过/跳过数量。
- [ ] 需要 RTL 的检查设置 `PAL_REQUIRE_RTL=1`，不把 simulator 缺失产生的 skip 当 pass。
- [ ] APF/grayscale 仿真、clock/phase 情况和故障回归通过；记录源码与日志 hash。
- [ ] probe ELF 独立生成；pal.pak 内只有合成图形/OGG 所需数据，未混入游戏素材。
- [ ] probe 保持独立 core/asset ID，不写存档。
- [ ] 已知 sanitizer 限制原样记录；没有把 ASan 无泄漏检测写成 LeakSanitizer 通过。

## C. firmware / netlist / FPGA

- [ ] 在独立修改后的 core 构建；未改动原 SDK 或旧候选文件。
- [ ] boot.bin、firmware.mif、os.bin 同次构建；保存全部 hash 和编译日志。
- [ ] `verify_boot_image.py` 通过；MIF 深度/覆盖/地址与 padding 正确，boot 字节非全 NOP。
- [ ] Vexii 网表由固定源码和 os25 配置生成；更改配置后已重新生成。
- [ ] 新 QSF 指向该网表、新 MIF 和补丁 RTL；地址适用于本机，variant/seed/宏正确。
- [ ] 每 job 独立 db/output，容器独立 HOME/TMPDIR；使用预备本地镜像，不混用宿主 Quartus。
- [ ] `quartus_map` 通过，完整 `.map.rpt` 和警告保留。
- [ ] `quartus_fit` 通过，完整 `.fit.rpt`、ALM/register/M10K 用量保留且在器件上限内。
- [ ] `quartus_asm` 通过，实际新 `.sof/.rbf` 生成并记录 hash。
- [ ] `quartus_sta` 与完整 timing report 保存；100 MHz 要求未偷降。
- [ ] setup/hold 全部审查，不只看 Fmax；负 slack、未约束路径与 CDC 例外均已处理。
- [ ] RGB 与控制 sideband 延时一致；blanking/APF 特征字不被转灰。
- [ ] 对桥接/视频修改运行匹配 core 的适用回归，记录未运行的测试和依赖原因。

只完成 C 的前四项是中间产物，不是新 bitstream。
Quartus 退出码为 0 也不等于 timing 已全部合格。

## D. runtime 与候选打包

- [ ] 在单独 staging SDK 明确导出新 ABI/runtime，没有覆盖原 pin。
- [ ] 新 `.rbf_r`、os.bin、loader 与 boot/header/musl 来源相容；无残留旧产物冒充新集。
- [ ] 新 manifest、pins、source+patch、工具/镜像、variant/seed、所有 hash 可追溯。
- [ ] 新打包代码/配置经过审查，拒绝混装、脏 SDK 和覆盖保护仍有效。
- [ ] 原 `package.py` 只打旧固定 SDK；未用改 checksum/删校验的方法绕过它。
- [ ] 默认保持 320×200 slot 1、4:3 normal/CRT；LCD profile 为明确选择。
- [ ] 若含 `0x20–0x23` 单色 ID，使用通过 C 的新 runtime；绝不装到旧 v1 RBF 上。
- [ ] 每个 video.json 最多 16 个非零 display mode ID；保留完整 scaler-slot ABI。
- [ ] 游戏/probe 名称、所选显示 profile、版本和“开发候选”标记与实际能力一致。
- [ ] 许可证/notice、完整对应源码、构建脚本及依赖身份已保留；无商业游戏素材/音乐/音色库。

## E. 真机记录（尚需实际 Pocket 和 SD 卡）

```text
设备 / Pocket firmware：
Dock firmware / HDMI 显示器（如使用）：
SD 型号 / 文件系统 / 备份位置：
本次 package SHA-256 / runtime manifest：
PAL 版本 / 语言 / 私有 pal.pak SHA-256：
OGG encoder / sample rate / channels / bitrate：
照片 / serial log / 音频记录位置：
测试时长 / 失败复现：
```

- [ ] 接手者有意将指定候选放入 SD，并先备份有价值存档；未由构建脚本自动部署。
- [ ] probe 四边框完整、单像素棋盘正确；默认 4:3 下源椭圆显示为圆，无裁切/额外行。
- [ ] normal ↔ CRT 重复切换正确；开机/reset/菜单/返回游戏不破坏显示。
- [ ] 选用单色模式时灰阶/基色正确，真实 APF `0x00B8` 完成响应被 host 接受。
- [ ] 快速重复切换、超时/恢复、warm reset 不把旧灰度请求当新 ack。
- [ ] LCD 每个已公布系列都在 handheld 与 Dock 记录实际比例；不把“JSON 写 4:3”当测量。
- [ ] Dock 拔插/重连、HDMI 输出和正常模式恢复正确（未测须写未测）。
- [ ] PAL 开机到标题、开始游戏、地图/战斗/菜单/RNG 动画通过；缺失/损坏 assets 有可见错误。
- [ ] D-pad/斜向/长按/短按/按钮、Dock 键盘、断连重连无重复或丢失事件。
- [ ] probe 连续运行至少 30 分钟，听左右声道、循环；记录 decode time/pump gap/欠载。
- [ ] 完整游戏音乐+频繁音效+地图加载/战斗至少 30 分钟，另记录 CPU/frame/内存高水位。
- [ ] 把启动填充瞬态和稳态欠载分开；稳态不持续饥饿，无明显可听断音。
- [ ] 五槽分别保存/同次读回；Pocket 菜单 Quit 后再启动读回状态和文件长度一致。
- [ ] 只更新 ELF 后存档仍可读；交替运行其它 core 不串槽；错误场景不破坏唯一好存档。
- [ ] 不用突然断电、sleep 或 reset 代替 Quit 测试；记录不支持断电安全。

probe 测试不能代替游戏逻辑、SFX 与真实加载负载。若解码性能不达标，先凭测量定位；
需要真正 FPGA Vorbis 时，应另行实现并验证该解码器，不能把现有 PCM 播放改名为 offload。

## F. 结论

```text
源码恢复：通过 / 失败 / 未运行
真实游戏 ELF：通过 / 失败 / 未运行
主机与 RTL 回归：通过 / 失败 / 有跳过
新 FPGA 全流程：通过 / 失败 / 未运行
时序与资源人工签核：通过 / 失败 / 未审查
新一致 runtime 候选：完成 / 未完成
真机显示/OGG/游戏/存档：通过 / 失败 / 未运行
剩余阻塞与下一步：
```

只按实际证据勾选；没有真机数据时最终结论仍是“开发候选，设备验证待完成”。
