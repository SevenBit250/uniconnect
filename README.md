# UniConnect — 带 CAN 的 CMSIS-DAP 探针

基于 CH32V305CCT6（RISC-V，主频 144MHz）的调试探针固件：SWD 调试 + CDC 虚拟串口 + 双路 CAN-USB 桥 + USB 固件升级（bootloader），运行 RT-Thread Nano。

## 功能一览（全部真机验证）

| 功能 | 状态 | 说明 |
|---|---|---|
| CDC 虚拟串口 | ✅ | 921600–9M 全档位零丢失（9M 实测 833KB/s 线速） |
| 双路 CAN-USB 桥 | ✅ | 1M 波特率，真总线双向千帧零丢失，WinUSB 免驱（MS OS 2.0） |
| CMSIS-DAP (bulk v2) | ✅ | OpenOCD/pyOCD 即插即用，FW 2.1.1 |
| SWD 目标调试 | ✅ | 对 STM32F103 完成全功能验证（断点/观察点/闪存算法），有效 SWCLK ~5.3MHz |
| USB 固件升级 | ✅ | APP 触发 → HID 传输 → CRC 校验 → 回 APP 全闭环，封板级加固 |
| LED 灯语 | ✅ | 5 灯状态系统（open/活动语义），boot 态 500ms 快闪可辨 |
| 宿主 SDK | 🚧 | uniconnect_upgrade / uniconnect_can 动态库（C ABI，`cmake --build build` 一并产出，协议实现第 2/3 期点亮） |

## 快速开始

### 前置条件

- [MounRiver Studio](http://www.mounriver.com/) ≥ 2.3（**仅取其工具链与 openocd**，构建已不依赖 IDE），默认装在 `C:\MounRiver\MounRiver_Studio2`；装在别处用环境变量 `TOOLCHAIN_BIN` / `OPENOCD_BIN` 覆盖（正斜杠路径）
- CMake ≥ 3.20 与 Ninja（`pip install ninja` 即可）
- 烧录需要 WCH-Link / WCH-LinkE 调试器

### 构建

```bash
cmake -B build -G Ninja                    # 一次配置（默认 Debug=-O0 -g，与历史验证位一致）
cmake --build build                        # 编译 APP + boot 双固件
cmake --build build --target uniconnect.elf   # 只编 APP；boot.elf 同理
cmake --build build --target flash_app     # 烧 APP（openocd + wch-link）
cmake --build build --target flash_boot    # 烧 boot
cmake --build build --target flash_all     # 单会话双烧 + 复位运行
```

产物在 `build/`：`uniconnect.{elf,bin,hex,map}` 与 `boot.{elf,bin,hex,map}`。

构建类型：`-DCMAKE_BUILD_TYPE=Release`（-Os，APP 37.6KB）。**切换 Release 是独立流程**——代码生成变化后必须全回归 + 栈水位（见下）。boot 的 Debug 特例映射 -O1（-O0 溢出 20KB 分区）。

### 出厂烧录流程（密封设备必读）

```
cmake --build build --target flash_all     # 烧 boot + APP
python tools/fw_update.py build/uniconnect.bin --skip-trigger   # 写入首份 GOOD 记录
# 断电重插（POR）→ 验证设备以 APP 形态枚举 → 才可密封
```

openocd 直烧后设备停在 boot 态（HID，PID 0x0219）是**预期行为**——无 GOOD 记录不放行是封板级安全设计。升级固件用 `python tools/fw_update.py build/uniconnect.bin`（APP 态触发，全自动两阶段）。

## 目录结构

```
app/            APP 固件工程（RT-Thread 全功能，flash@0x5000）
  Application/    线程层（CDC/DAP/CAN/monitor）
  Bsp/            硬件抽象层（bsp_* API，依赖严格单向向下）
  Dap/            CMSIS-DAP 官方源码（硬件相关全在 DAP_config.h）
  User/           main / config.h（线程优先级与栈集中定义）/ 中断 / debug
  app.ld          链接脚本
boot/           bootloader 固件工程（裸机 + CherryUSB 单 HID，flash@0x0/20KB）
common/         公共层：pin.h（全板引脚单一事实源）、Core/Peripheral/Startup、
                boot_shared.h（boot/APP 契约）、system_ch32v30x.h
third_party/    CherryUSB（vendored v1.51）、RtthreadNano（vendor 化 v3.1.5）、
                RtthreadNanoPatch（补丁层）
cmake/          toolchain.cmake + flash.cmake（烧录目标）
sdk/            宿主 SDK 动态库（uniconnect_upgrade/uniconnect_can，C ABI，见 sdk/README.md）
tools/          测试/诊断/升级工具（见 tools/README.md）
docs/           设计文档（bootloader 设计、SPI 加速调查等）
```

换板改引脚只动 `common/pin.h`；boot 与 APP 的分区契约只在 `common/boot_shared.h`（与两个 .ld 三方同步）。

## 开发指南

**动手前先读 [AGENTS.md](AGENTS.md)**——它是本仓的操作手册，沉淀了全部硬约束（目录分层铁律、已实锚的硬件坑、测试方法论）。要点速览：

- **分层**：`app/Application → app/Bsp → common/Peripheral`，严格单向向下；Bsp 只通过 `bsp_*` API 暴露硬件；新增 Bsp 源文件要同步加进 `app/CMakeLists.txt`
- **改 MS OS 描述符必跑** `python tools/msos_validate.py`（机械校验，勿肉眼对字节）
- **改线程栈配置必跑** `python tools/stack_watermark.py`（多构建目录加 `--map build-rel/uniconnect.map` 指对 map）
- **改 CAN 换模式逻辑必跑** `tools/can_bus_test.py`（真总线，静默回环掩盖不了引脚/FIFO 缺陷）
- **固件改动回归顺序**：`cdc_loopback_test.py` → `can_usb_test.py` → DAP link 检查 → `fw_update.py` 端到端（详见 `tools/README.md` 回归套件表）
- **调试利器**：openocd `halt + mdw` 直读运行固件 RAM（黑匣子 0x2000F000 / g_dbg 计数器 / 魔法波特率 1234567 快照）

### 烧录与诊断注意（实锚教训，别再踩）

- `openocd flash erase_address` 在 WCH SDI 驱动上是**整片擦**，禁止使用；烧录后若 USB 不重枚举需物理重插
- openocd flash 操作后不要 `resume`（FLASH_CTLR 脏态 + 固件执行 = 整片擦除）
- 调试进 boot 态可用 `openocd` 写 BOINIT 旗标（`mww 0x2000D000 0x5AA55AA5` + `reset run`）配合 `fw_update --skip-trigger`，无需物理操作

## 贡献代码

1. **Fork + 分支**：从 `main` 拉特性分支开发，勿直接推 `main`
2. **提交规范**：Gitmoji 前缀 + 短中文描述（风格见 `git log`），如 `:sparkles: xxx` / `:bug: xxx`；一次提交聚焦一件事
3. **必须自证的改动类别**（提交说明里写明测试结论）：
   - USB 描述符 → `msos_validate.py` 输出
   - 线程栈 → `stack_watermark.py` 全绿输出
   - 数据通路 → 对应回归脚本结果（CDC/CAN/DAP）
   - bootloader → `fw_update.py` 端到端 + 中断注入测试（传输中途杀进程 + 复位，必须仍留 boot 态）
4. **boot 分区冻结纪律**：boot 已过封板级验证，非必要不动；动了必须重走注入测试全套
5. **文档同步**：改目录/构建/契约时同步更新 AGENTS.md 与 docs/——本仓文档即规格

## 更多文档

- [AGENTS.md](AGENTS.md) — 构建与调试、目录分层、已知坑全集（DMA 狂奔、HPE 硬件栈、MS OS 2.0 排查史、SysTick 风暴等）
- [docs/bootloader-design.md](docs/bootloader-design.md) — bootloader 分区/协议/风险设计
- [tools/README.md](tools/README.md) — 全部测试与诊断工具用法
- [docs/spi-accel-swd-worklog.md](docs/spi-accel-swd-worklog.md) — SWD SPI 加速引擎调查（v2 板恢复路线）
