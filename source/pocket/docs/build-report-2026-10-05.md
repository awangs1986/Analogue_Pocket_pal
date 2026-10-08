# Pocket 移植构建报告 · 2026-10-05

## 当前结论

已得到实际 RISC-V ELF 和可检查的 Pocket 开发包，并非只有接口空壳或文档。
这是「可进入真机验收的开发候选」，不是已经在 Pocket 通关、稳定运行或通过
FPGA 时序签核的正式版本。没有下载/打包仙剑版权素材，没有写入 SD 卡、烧录、
push、创建 PR 或合并代码。

原仓库基线：`1d35d896b99722b9968685c1284cb5f1566cd662`。
本地工作分支：`pocket/riscv-port`。新增 `pocket/` 和根 Makefile；原 MiSTer
CMake、HPS 后端、SDLPAL 引擎与内嵌编解码源码未改。

## 已实现

- 原生 320×200、8-bit 调色板画布；按实际 pitch/stride 完整拷贝，保持像素。
  使用原生 scaler slot 1，Pocket 配置为 4:3；不补成 224/240、不软件拉伸/裁切。
- 原生按键、双手柄合并、摇杆阈值、Dock 键盘、短按锁存与重复键状态机。
- 真实 Ogg/Vorbis 软件解码、8–48 kHz 单/双声道、重采样、循环、音效混合、
  有界 PCM 环、短写保留、欠载和解码计时。FPGA 负责 PCM 输出，不冒称 FPGA
  已经解码 OGG。
- 一个 seekable `pal.pak` 资源包与五个存档槽；路径和文件边界校验；失败可见。
- Native/CRT 配置；其它 LCD/单色模式不宣称支持。Windows AVI 真彩视频禁用，
  DOS/RNG 动画保留。
- 无版权游戏素材的独立 RISC-V probe：320×200 边框/像素/比例图案与合成 OGG，
  实测解码预算、主循环间隔和欠载；独立 core/asset ID，绝不写存档。

## 实际运行过的检查

1. 官方 Debian GCC 14.2.0 + binutils 2.44 工具链，软件包 SHA-256 已校验。
2. 干净目录完整交叉编译/链接成功：
   `make -C pocket BUILD_DIR=build/verified OBJ_DIR=build/verified/obj CROSS=/path/to/local/toolchains/debian/root/usr/bin/riscv64-unknown-elf- -j4`
3. 默认构建和干净构建 ELF 的 SHA-256 完全一致：
   `4425cee99b60d81577bc4c8e6948f51c7c9bedaf08984f8ee27ee848f4cfcd60`
4. ELF 检查：ELF32、little-endian、RISC-V、RVC/single-float ABI，入口
   `0x10400000`；无动态解释器、无未解析符号。对象属性验证为 rv32imafc。
   SDK 链接脚本会丢弃最终 ELF 的属性段，所以 ISA 同时按对象及构建命令核验。
5. 主程序 text 1,642,008 bytes；data 2,163,496 bytes；BSS 226,680 bytes。
   这不是峰值运行内存或加载完全部游戏数据后的内存测量。
6. `make -C pocket check`：19 项输入/显示/存档/打包/错误界面测试 + 10 项
   OGG/音频适配测试全部通过。视频和输入测的是实际后端配测试服务，而不是另写
   一套相似逻辑。文件测试含 GNU linker `--wrap` 拦截与 17 类损坏包。
7. `ASAN_OPTIONS=detect_leaks=0 PPA_TEST_CFLAGS='-O1 -fsanitize=address -fno-omit-frame-pointer' python3 pocket/tests/audio_test.py`：10 项通过。
8. 显示/输入/错误界面测试带 UBSan。音频完整 UBSan 暴露原有 vendored
   liboggvorbis 的 signed-shift UB；未修改共享编解码源码，不声称全套 UBSan 清洁。
   当前环境下 LeakSanitizer 不能运行，未声称做过泄漏验证。
9. 三项临时变异测试确认能抓住：错误 framebuffer stride、调色板 R/B 交换、
   A 键错误映射为取消。
10. 精确 runtime 源码核对与独立审查：SDK 的 services/input/caps/app ABI
    头与 runtime `618a3eb985759a4154115109c2c8036271252888` 一致。
    修复了空存档被当有效存档、黑色调色板使报错不可见、CD 解码失败仍显示播放、
    OGG 文件名文档不匹配等问题。`git diff --check` 通过。

## 固定依赖

- SDK：`a408ddc12aed0dfaa4aa22c06af82f829db77126`
- SDK 自带 runtime：manifest source `618a3eb`，os25 变体
- os25.rbf_r SHA-256：`584885133d6cf45ba35480ce8e8766150f67ca47f9019c6f771b12e89cca12c1`
- os.bin SHA-256：`7ee410f1e22dbf9ecb696cb0a9541ab532d128d9d36440f71081dbe3c4fa5506`
- loader.bin SHA-256：`fc1f8d37eb0fb322006b4428f0b59d2801c9b74978bd3f24103f5f9bbfca3c99`
- 探针 ELF SHA-256：`0b4012875175745b76fd5707bddfdfc45dc0e0c6efbecfc2105687871e802065`

打包工具强制验证 SDK pin 和 runtime manifest/checksum，拒绝混装或静默覆盖。
最终发行文件哈希在外层交付包的 SHA256SUMS 中，避免报告与自身归档哈希循环依赖。

## 仍未验证，必须保留的边界

- 物理 Pocket 开机、进入游戏、地图/战斗、真实文字素材和语言版本兼容性。
- 100 MHz 上持续 OGG + 音效 + 文件加载的性能、最长暂停、真实可听欠载。
  尚未判断 CPU 不足，故没有凭猜测实现或宣称完成 FPGA Vorbis 解码器。
- Pocket 画面真实 4:3、CRT 视觉表现、Dock 与手柄断连恢复。
- 存档关闭是软件提交；**菜单 Quit 后才进行 SD 持久化**。须测试退出/重启读回；
  不支持睡眠，不承诺突然断电安全。空 256 KiB sentinel 已拒绝并加回归。
- 未运行 Quartus、未重建 FPGA bitstream、未证明 100 MHz 时序闭合。
- 没有合法游戏数据在本环境中，因此没有运行完整游戏；host tests 不能代替真机。

后续可先授权把独立 probe 包放入 Pocket SD 卡并收集实测；随后提供自己合法持有的
游戏数据及 OGG，制作私有 pal.pak，按 acceptance.md 完成游戏验收。本次没有代为
执行上述硬件动作。上传/发布仓库或 PR 仍须另行授权。
