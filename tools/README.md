# tools/ — 测试、验证与调试工具

所有 Python 工具依赖 `pyserial`/`pyusb`（`pip install pyserial pyusb` + libusb1 后端）；CAN 类脚本默认 VID/PID `0x0D28/0x0218`、接口 3（改动接口布局时须同步脚本 `IF_NUM`）。

## 铁律工具（改动对应内容后必须跑）

| 工具 | 作用 | 用法 |
|---|---|---|
| `msos_validate.py` | MS OS 2.0 描述符集机械校验：结构平铺、子集长度、集合/BOS 双份总长同步、类型值（规范表 9）。改 `Bsp/device/usb/bsp_usb.c` 的 MSOS/BOS 必跑，勿肉眼对字节（曾两次拦下丢字节/漏同步） | `python tools/msos_validate.py` |
| `stack_watermark.py` | 线程栈高水位检测：openocd 读栈（RT-Thread '#' 预填单调保持，事后扫描等价实时），按 map 解析各栈实占与利用率，默认 60% 报警。改任何线程栈配置后必跑 | `python tools/stack_watermark.py --limit 60` |

## 回归套件（固件改动后按此顺序跑）

| 工具 | 作用 | 用法 |
|---|---|---|
| `cdc_loopback_test.py` | CDC 基础回环（板上 UART TX-RX 短接）：枚举/大流量/逐字符/线控切换 | `python tools/cdc_loopback_test.py` |
| `cdc_baud_sweep.py` | 波特率全档位扫描（921600–9M），各档 64KB 并发分块回环，记录正确性与吞吐 | `python tools/cdc_baud_sweep.py [PORT]` |
| `cdc_hammer.py` | 满载长锤：指定波特率/单轮字节数/轮数，字节级校验（性能边界验证用） | `python tools/cdc_hammer.py COM10 5000000 262144 5` |
| `cdc_soak2h.py` | 四工况真实负载 soak（满载/小包高频/等差+sin+随机混合/超大），逐批 MD5 校验 + 魔法波特率计数器对账，结果写 `soak_result.txt`（gitignore） | `python tools/cdc_soak2h.py COM10 5000000 1800` |
| `can_usb_test.py` | CAN-USB 桥双路回归：静默回环模式双路各 1000 帧 + 状态计数对账 | `python tools/can_usb_test.py` |
| `can_loopback_read.py` | CAN 双路静默回环自测读数：两次快照取增量验证 tx==rx 且 err/drop==0 | `python tools/can_loopback_read.py [PORT] [间隔秒]` |

DAP 通路：`openocd -c "adapter driver cmsis-dap" -c "transport select swd" -c "init" -c "cmsis-dap info" -c shutdown` 能读全 link 信息即 PASS。**SWD 目标调试**（勿用 `-f target/*.cfg`——WCH 版 openocd 脚本搜索前缀损坏，用手工 DAP 声明）：

```bash
openocd -c "adapter driver cmsis-dap" -c "transport select swd" -c "adapter speed 1000" \
  -c "swd newdap stm32 dp -expected-id 0x1ba01477" \
  -c "dap create stm32.dap -chain-position stm32.dp" \
  -c "target create stm32.cpu cortex_m -dap stm32.dap" \
  -c init -c targets -c shutdown
```

（STM32F103 实测：DPIDR 0x1BA01477，Cortex-M3 r1p1，完整 DP/AP 枚举通过。接线：探针 PB3→SWCLK/PA14、PB5→SWDIO/PA13、共地。）

## 诊断工具（出问题再用）

| 工具 | 作用 |
|---|---|
| `cdc_diag.py` | CDC 64KB 故障定位：并发读写/移位分析/固件计数器快照（魔法波特率 1234567） |
| `cdc_delta_exp.py` | 字节账目对照实验：快照→混合负载→快照，四点对账 + ORE 定位（分层排除法的方法论工具） |
| `cdc_mixed_focus.py` | 混合负载专项复现+取证：等差/sin/随机轮转，失败时分析缺失切片结构 |

## Windows / 硬件侧辅助

| 工具 | 作用 |
|---|---|
| `check_device.ps1` | PnP 速查：本机 VID_0D28 设备状态/问题码（枚举问题第一现场） |
| `restart_device.ps1` | 设备节点禁用/启用强制重枚举（需管理员） |
| `admin_etrace2.ps1` | USB 枚举 ETW 追踪（USBHUB3/USBCCGP/USBXHCI/PnP，需管理员）；抓 `STATUS_NO_SUCH_DEVICE`/描述符判废类问题的现场，日志过滤 VID_0D28 |
| `watch_hunt.bat` | 观察点猎手：openocd 轮询 rt_thread_defunct 与看门狗槽，命中记录 PC 与现场 |

## 固件内建调试设施（配合上述工具）

- **魔法波特率 1234567**：CDC 打开后设为此波特率，固件经 CDC IN 返回 `g_dbg` 全链路计数器快照（84B，29 个 u32），各诊断脚本据此对账。
- **崩溃黑匣子 0x2000F000**：HardFault 现场（mcause/mepc/mtval/ra/sp）+ 看门狗槽[12..15]，`openocd halt + mdw` 直读。
- **openocd 观测**：`halt + mdw <map符号地址>` 可读运行固件任意 RAM/寄存器（配合 `riscv-none-embed-nm obj/uniconnect.elf` 查地址）。
