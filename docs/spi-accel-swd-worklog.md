# SPI 加速 SWD 工作记录（暂存，等 v2 板）

**状态**：暂存。v1 板硬件缺 MISO 桥接，SPI 全双工方案无法验证。
**日期**：2026-10-01。**基线**：GPIO 位拍引擎工作正常（有效 SWCLK ~5.3MHz，RAM 读 106KiB/s）。

## 目标

把 SWD 时钟从 GPIO 位拍上限（~5.3MHz）推向 SPI 硬件上限（36MHz = APB2 72M/2），
对标社区高速 DAPLink（50MHz 级）。

## 已完成的工作（代码在库，`BSP_DAP_USE_SPI` 宏切换）

1. **引擎架构**（`Bsp/device/dap/bsp_dap.c`）：
   - 强符号覆盖官方 `__WEAK` 的 `SWJ_Sequence` / `SWD_Sequence` / `SWD_Transfer`
     （官方 CMSIS-DAP v2.1 预留的板级加速钩子，vendored 零侵入）；
   - 字节级移位 + 相位位偏移切片：线上协议逐位不变，SPI 每 8 线位打包一字节，
     turnaround/ack/data 按 `DAP_Data.swd_conf.turnaround` 偏移提取；
   - `DAP_SWJ_Clock` 钩子（DAP.c 一行）让 SPI 分频跟随主机请求时钟。
2. **配套修复**（这些是独立有效的，已合入）：
   - `bsp_dap.c` 补 `#include <rtthread.h>`（曾致 INIT 注册被隐式声明吞掉）；
   - boot 期自启动 init（`INIT_COMPONENT_EXPORT`，不依赖主机发 DAP_SETUP）；
   - **SWJ_Disable 彻底移除**（见铁律：运行期切断 SDI，且 PB3/PB4 本无需释放）；
   - SPI1 重映射与 CAN1 重映射在 PCFR1 正确共存（顺序：先 SPI1 后其他，
     厂商库读改写会污染 SWJ_CFG 位）。
3. **实测进度**：枚举链路未通（ack 恒 0）。固件侧已验证健康：SPI1 使能、
   BIDIMODE 配置生效、重映射生效、SDI/USB/CAN 三线共存。

## 未通的根因：v1 板硬件不支持所选路线

调查落点（社区参考，2026-10-01）：

1. **单线半双工（BIDIMODE / SPI_3WIRE）路线**——[lupyuen/openocd-spi](https://github.com/lupyuen/openocd-spi)（树莓派 bcm2835spi.c）：
   - 证实可行但代价惨重：**每次读事务后目标处于混乱状态，必须重发
     JTAG-to-SWD + IDCODE 预取 + ABORT 清 overrun**；写事务**伪造 ACK 盲写**；
   - 它靠 31MHz 高速摊薄每次读的重同步税。我们卡在 CH32 BIDIMODE-Rx 的
     主机时钟语义（RM 无确证、EVT 无主机侧例程），与参考实现的行为差异无法闭环。
2. **全双工 + 串联电阻桥路线**——[probe-rs](https://pigweed.googlesource.com/third_party/github/probe-rs/probe-rs)、
   [ST 社区例程](https://community.st.com/stm32-mcus-boards-and-hardware-tools-26/jtag-swd-nucleo-over-spi-code-example-123354)：
   - **MISO 直连 SWDIO，MOSI 经 ~1K 串联电阻连 SWDIO**。方向永远不切：
     读相位 MOSI 弱驱 1（目标强驱赢，MISO 读真值）；写相位目标三态、MOSI 经电阻驱动；
   - 无 BIDIMODE、无方向切换、写 ACK 可读（比树莓派路线更正确）；
   - SPI mode 0（CPOL=0 第一边沿采样）、软件位反转（硬件 MSB vs SWD LSB）。
   - **此路线需要 v2 板布 MISO 桥 + 电阻。**

## v2 板需求（回来后按此恢复）

1. **PB4（SPI1_MISO）布线到 SWDIO 网络**；
2. **PB5（SPI1_MOSI）经 470Ω~1K 串联电阻接 SWDIO**（电阻越大越"礼貌"，
   边沿越缓；1K 是社区常用值）；
3. SWCLK（PB3）不变；
4. （可选）SWDIO 附近留测试点，方便示波器看眼图。

## 恢复步骤

1. `Bsp/bsp_config.h`：`BSP_DAP_USE_SPI` 置 1；
2. 重写 `bsp_dap.c` SPI 后端为**全双工**（删掉 BIDIMODE/方向切换/SPE 门控）：
   统一 `spi_xfer(tx, rx, n)`，读事务 = xfer 6 字节（req + 5），MISO 流按位偏移
   切片（现有 getbits/REV 基建直接复用）；写事务 = xfer（req + dummy(电阻弱驱) +
   data33），ACK 从 MISO 免费读到——不需要树莓派式的盲写；
3. 参照本文档两条社区参考的行为细节（事务尾 ≥8 空闲时钟等）；
4. 速度曲线复测（`dump_image`/`load_image` RAM 吞吐法）。

## 本次调查的副产物（独立沉淀）

- 官方 `__WEAK` 钩子机制确认：加速引擎可以零侵入 vendored 代码；
- 自杀性测试铁律 + 抢救权归属用户（见 AGENTS.md 开发铁律）；
- WCH openocd 的 `program` 对读保护芯片直接失败，解锁走 WCH-LinkUtility
  （MRS 内置路径 `components/WCH/Others/SWDTool/default/WCH-LinkUtility.exe`）。
