# Pocket FPGA 时序收敛报告：tc6-s16（测试版，未经真机验证）

## 结论

- 补丁系列 `pocket/fpga/timing-closure-tc6`（4 个补丁，基于 openfpgaCore `618a3eb` + `display-modes.patch`）
  配合 **seed 16**，得到唯一一个 4 个工艺角 setup / hold / recovery / removal / mpw **全部为正**的构建：**tc6-s16**。
  时钟目标 100 MHz 不变，没有放宽约束，也没有加不合法的约束。
- 这是对 seed 敏感的结果：同一系列 11 个 seed 里只有 1 个全正。第 7 轮的 hold 不确定度方案是 0/10，不采用。
- **没有在 Pocket 或 Dock 上运行过。** GPU 换成了空壳；build_id 为 0；x_count 跨时钟域的 hold 余量仍然很小，需要 RTL 修复（见“已知限制”）。

## 环境

| 项 | 值 |
|---|---|
| 器件 | Cyclone V 5CEBA4F23C8 |
| 工具 | Quartus Prime 25.1std.0 Build 1129 Lite（容器），QPROCS 4 |
| 变体 | os25；宏 `INCLUDE_ANALOGIZER INCLUDE_HW_MIXER INCLUDE_4PLAYER EXCLUDE_GPU` |
| 基线 | openfpgaCore `618a3eb` + `pocket/fpga/display-modes.patch`（sha256 `62c99cdf…09a8`），由 `prepare_runtime.py` 生成 |
| 系列 | 原 tc 分支 `3187d74..7c29497`，共 **4** 个 commit（acc7a24、2cfb938、0b10bdd、7c29497） |

## 各轮结果

WNS/TNS 单位为 ns。“85 °C”/“0 °C”指 Slow 1100mV 模型的 setup；“快角 hold”指 Fast 模型最差 hold。

### 基线与早期轮次（seed 40，除非注明）

| 轮次 | 内容 | 85 °C WNS / TNS | 备注 |
|---|---|---|---|
| 基线 | 618a3eb + 显示补丁（pb fpga 阶段） | −0.664 / — | 起点 |
| tc1 | 试验 commit 3c43e7c（未进入系列） | −0.708 / −34.687 | 放弃 |
| tc2 | 试验 commit bdb9209（未进入系列） | −0.914 / −31.253 | 放弃 |
| tc3 | acc7a24 SDRAM DQ 选择寄存化（= 补丁 0001） | −0.734 / −56.436 | seed 扫描见下表 |
| tc4a | 2cfb938 精简 GPU 功能（= 0002），seed 8 | −0.706 / −47.89 | ALM 14,376，M10K 285 |
| tc4b | 9f8a299 物理综合（未进入系列），seed 8 | −0.835 | 放弃 |

tc3（acc7a24）seed 扫描，ALM 约 15.8–15.9k / 18,480：

| seed | 85 °C WNS / TNS | 0 °C WNS / TNS | 85 °C 失败路径数 |
|---|---|---|---|
| 40 | −0.734 / −56.436 | −0.640 / −39.320 | 1,558 |
| 1 | −1.031 / −265.298 | −0.871 / −185.010 | ≥5,000 |
| 2 | −1.071 / −163.115 | −0.819 / −130.889 | 3,380 |
| 3 | −0.953 / −288.878 | −1.128 / −238.469 | ≥5,000 |
| 4 | −1.279 / −452.899 | −0.958 / −319.213 | ≥5,000 |
| 5 | −1.203 / −290.678 | −1.208 / −215.700 | ≥5,000 |
| 6 | −1.359 / −112.737 | −1.237 / −93.249 | 4,004 |
| 8 | −0.642 / −12.484 | −0.452 / −8.350 | 385 |

### tc5：0b10bdd `EXCLUDE_GPU` + gpu_min 空壳（= 0003）

ALM 约 10.8k，M10K 243。

| seed | 85 °C WNS / TNS | 0 °C WNS |
|---|---|---|
| 40 | −0.819 / −12.928 | — |
| 1 | −0.436 / −3.399 | — |
| 2 | −0.782 / −10.492 | — |
| 3 | −0.828 | — |
| 4 | −0.839 | — |
| 5 | −0.705 / −22.022 | — |
| 6 | −0.186 / −3.442 | — |
| 8 | −0.152 / −0.754 | —（本轮最好） |

### tc6：7c29497 对不用的 DDIO 上升沿采样寄存器加 false path（= 0004）

只对 16 个确认不用的 `dataout_h` 节点加 false path，真正使用的 DFFLO 采样仍受约束。

| seed | 85 °C WNS / TNS | 0 °C WNS | 快角 hold |
|---|---|---|---|
| 2 | −0.143 / −0.363 | −0.121 | −0.034 |
| 4 | −0.219 / −0.812 | +0.076 | −0.044 |
| 8 | −0.215 / −0.830 | −0.176 | +0.084 |
| 9 | −1.063 / −16.57 | −0.867 | +0.082 |
| 10 | −0.249 / −1.701 | −0.105 | −0.098 |
| 11 | −0.180 / −1.026 | −0.089 | +0.064 |
| 12 | −0.057 / −0.102 | −0.005 | +0.043 |
| 13 | −0.083 / −0.087 | −0.131 | +0.042 |
| 14 | −0.352 / −1.585 | −0.395 | +0.015 |
| 15 | −0.252 / −2.789 | +0.021 | +0.006 |
| **16** | **+0.097 / 0** | **+0.272** | **+0.009（全正）** |

全正比例：1/11。

### tc7（不采用）：只在 fitter 阶段对 general[3]↔general[2] 加 `set_clock_uncertainty -hold -add 0.15`

用 tc6-s16 的数据库做 STA 对照，结果与原来相同（只有 SDC 行号和并列顺序不同），说明约束对 sign-off 无副作用；但重新布局后 0/10 全正：

| seed | 85 °C WNS / TNS（失败数） | 0 °C WNS | 最差 hold |
|---|---|---|---|
| 16 | −0.116 / −0.208（12） | −0.058 | +0.121 |
| 2 | −0.093 / −0.149（6） | −0.052 | +0.116 |
| 4 | −0.039 / −0.089（4） | +0.163 | +0.078 |
| 8 | −0.185 / −0.717（10） | −0.098 | +0.105 |
| 10 | −0.113 / −0.826（19） | +0.050 | −0.171（x_count[1] 所有角都失败） |
| 11 | −0.120 / −0.291（18） | −0.090 | +0.091 |
| 12 | −0.339 / −2.489（58） | −0.339 | +0.119 |
| 13 | −0.154 / −0.457（10） | −0.131 | −0.026（x_count[9]） |
| 14 | −0.463 / −1.048（28） | −0.197 | +0.059 |
| 15 | −0.222 / −1.481（18） | −0.216 | +0.118 |

结论：tc7 不合并，以 tc6-s16 作为签核构建。

## tc6-s16 签核数据

最差余量（ns）：

| 工艺角 | setup | hold | recovery | removal | mpw |
|---|---|---|---|---|---|
| Slow 85 °C | +0.097 | +0.288 | +2.302 | +0.996 | +0.500 |
| Slow 0 °C | +0.272 | +0.281 | +2.306 | +0.930 | +0.500 |
| Fast 85 °C | +1.998 | +0.009 | +6.453 | +0.377 | +0.500 |
| Fast 0 °C | +2.052 | +0.018 | +6.646 | +0.344 | +0.500 |

- 最差 hold（Fast 85 °C +0.009）：`analog_pixel_color` → Analogizer `pr_1b`，general[2] → general[3]。
- x_count 跨时钟域 hold：Fast 85 °C +0.102，Fast 0 °C +0.076。
- SDRAM DQ 读：setup +4.695 / hold +7.935（Slow 85 °C）。
- 资源：ALM 10,838 / 18,480；寄存器 17,370；M10K 243 / 308；DSP 17；引脚 224 / 224。
- 唯一的 critical warning：127003（缺 `build_id.mif`）。未约束端口：输入 10、输出 61。

产物 SHA-256（产物本身**不在仓库里**）：

| 文件 | SHA-256 |
|---|---|
| `os25.rbf_r`（1,861,212 B，= ap_core.rbf 按位反转） | `4c334a81dd6e6b8de9cec8c491699be34b71c44689ef73339e1e44a1becb7c06` |
| 内嵌固件 `firmware.mif` | `0b33fc685721744f03d886986bfeec875392fa534ef5a8ef62739cde1bd46b37` |
| 同一次固件构建的 `os.bin` | `bfd5e0043e2bfe77a46fa204260842e6980b58393a7f115b2bb792bb344c1b84` |
| 测试包使用的 `pal.elf` | `dbaea4bddad859a240685a25e9648944fa6b6ec2664f1108defb8893143aab9e` |

## 复现

见 `pocket/fpga/timing-closure-tc6/README.md`：`prepare_runtime.py` → `apply_series.py`（写入 seed 16）→
核心自带的 Make/Quartus 流程 → `pocket/tools/package_tc6.py` 生成独立测试包（核心 ID `awangs1986.PALtc6`、
平台 `sdlpaltc6`，不含任何游戏数据）。真机测试清单：`tc6-s16-test-checklist.md`。

系列**没有**接入 `build_handoff.py`（pb）：pb 在 prepare-core 之后会校验已准备核心的摘要，打补丁会让后续阶段拒绝该目录。
所以系列作为独立、可选的步骤，现有 pb 校验不受影响。

## variants.json 修复

真机报 “load error in variants”：核心目录缺 `variants.json`。按 SDK 模板加入了空列表
`{"variants":{"magic":"APF_VER_1","variant_list":[]}}`：

- `pocket/config/variants.json`（新增）；
- 项目自带的 `pocket/tools/package.py` 原来只复制 6 个 JSON（core/audio/data/input/video/interact），**同样漏了 variants.json**。
  现在改为常量 `CORE_JSON_FILES` 并包含它，`tests/test_package.py` 增加了回归测试；
- `package_tc6.py` 也使用同一个列表。

## 已知限制

1. **未经真机验证**：没有在 Pocket 或 Dock 上运行过；必须先按测试清单验证。
2. **GPU 空壳**（`EXCLUDE_GPU` + `gpu_min.v`）：寄存器兼容、立即完成。PAL 用软件绘制，所以够用；依赖 GPU 绘制的程序不会显示。
3. **build_id 为 0**：构建时缺 `build_id.mif`，设备上显示的构建 ID 和日期不可信。
4. **x_count CDC hold 余量小且对 seed 敏感**（tc6-s16 最小 +0.076 ns；tc7 的 seed 10/13 出现负值）。
   计划的 RTL 修复（未实现）：在 clk_analog 域里 `vid90_q <= clk_vid_90deg`，`issue_lcd_read = ~vid90_q & vid_run`，
   去掉 `lcd_x_count_prev`。实现前需要仿真，特别是第 0 位失败会丢一个像素读取。
5. 换 seed、换工具版本或改任何 RTL，都需要重新做完整的四角时序检查，不能沿用本报告的数字。
