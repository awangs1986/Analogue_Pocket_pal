# 统一源码交接：验证记录（2026-10-05）

## 已实际验证

- 全部当前主机/构建编排/RTL 测试：**61 项通过，无跳过**。
- 独立 OGG 音频测试：**10 项通过，无跳过**。
- 其中构建驱动有 32 项回归，离线展开有 4 项回归；覆盖固定依赖、迁移路径、
  缺工具/器件/磁盘、错误退出、镜像变化、缓存边界、产物篡改与旧 runtime 混装拒绝。
- Icarus 使用真实 patched bridge/video RTL 与上游 synchronizer；包含多时钟比例、
  超时取消/回滚和九个故意破坏行为的 mutation 检查。此处是行为仿真，不是布局布线。
- 从统一包的本地 Git bundles 恢复到全新工作路径，全部精确依赖可 checkout。
  Git bundle 本身不保存浅克隆边界；已添加校验覆盖的 `.shallow` sidecar 并在
  恢复时运行 `git fsck --connectivity-only`，再作后续 local clone 回归。
- 原始 core 的精确版本可在新路径应用显示补丁；QSF 在新路径重新生成，含正确
  `5CEBA4F23C8`、os25 CPU、灰度模块、seed 40 和四处理器设置，没有沿用旧云路径。
- 在恢复的完整源码和 SDK 上，用现有官方 Debian RISC-V GCC 14.2 工具链进行了
  游戏及 probe 的真正干净交叉编译。两个 ELF 与已验证 v1 候选逐字节一致：

| 产物 | SHA-256 |
| --- | --- |
| 游戏 `app.elf` | `4425cee99b60d81577bc4c8e6948f51c7c9bedaf08984f8ee27ee848f4cfcd60` |
| 合成 probe `app.elf` | `0b4012875175745b76fd5707bddfdfc45dc0e0c6efbecfc2105687871e802065` |

ELF 是 ELF32 RISC-V、RVC/single-float，入口为 `0x10400000`；游戏 ELF 没有未定义符号。
这些是实际 host-toolchain 编译结果，不冒充本环境已运行过 Docker 发行构建。
原 MiSTer 和共享引擎源码未改写；展开的相应源码字节保持一致。

## 独立审查与修复

独立审查实际克隆依赖、应用补丁并重新生成 QSF，发现并验证修复了浅克隆边界丢失；
还发现缓存输入包含工作目录时可能递归复制，已对称拒绝包含关系，并只复制明确的
sbt/Scala 缓存目录。镜像 ID 变化、旧编译对象、可疑路径字符和改动后复用工作目录
会被明确阻止，避免静默构建旧代码。

源码包不含原项目 Git 历史或其无关 Windows 签名私钥文件。五个依赖的可达历史做了
有限的凭据文件名和 PEM 私钥标记检查，未发现对应指标；这是有范围的检查，不宣称
完整安全审计。游戏版权素材、账号会话、工具安装包与 Docker 镜像均未加入本包。

## 必须保持“未执行/未验证”的项目

- 真实 Docker 编排执行：未运行，当前验证为 fixture/命令检查和独立本地 Git/QSF 操作。
- 新灰度 runtime 的 Quartus map、fit、asm、STA：**未执行**。
- FPGA 资源可容纳、所有 setup/hold 时序收敛、CDC 物理实现：**未验证**。
- Pocket/Dock 的显示模式接受、LCD 整数缩放比例、画质及动态切换：**未验证**。
- 100 MHz 上 OGG 连续解码加游戏负载、真实音频欠载、存档 Quit 后 SD 持久化：**未验证**。
- FPGA Vorbis 解码 offload：**未实现**；已实现的是 RISC-V 解码与 FPGA PCM 输出。

`candidate-v1/` 保留原 SDK 的匹配 runtime，不含本次灰度新 bitstream。它与源码补丁是
不同状态的交付项；不允许把编排 fixture 通过或 ELF 编译成功当作“全部滤镜移植完成”。

## 证据位置

统一包顶层 `evidence/` 提供测试输出、迁移编译日志、ELF 头与哈希、独立检查记录。
顶层 `SHA256SUMS` 与 `handoff-manifest.json` 覆盖实际源码和依赖；先核对再恢复。
后续自己编译的阶段日志和验收方法见 `BUILD_SPEC.md` 与 `README_构建交接.md`。
