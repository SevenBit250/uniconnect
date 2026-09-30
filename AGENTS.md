# AGENTS.md

UniConnect：基于 CH32V305CCT6（RISC-V，主频 144MHz）的 CMSIS-DAP 调试探针固件，目标功能为 SWD 调试 + CDC 虚拟串口 + 双路 CAN（CAN 未开发）。运行 RT-Thread Nano。

## 构建与调试

- IDE 为 MounRiver Studio (MRS) 2.3.0（Eclipse CDT 托管构建），项目配置在 `.project` / `.cproject`，勿手改 `obj/` 下自动生成的 makefile。
- 命令行构建：`cd obj && make`（前提是 IDE 已生成 makefile）。产物为 `obj/uniconnect.elf`。
- 不依赖 IDE 的本地构建：工具链在 `C:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\components\WCH\Toolchain\RISC-V Embedded GCC\bin`，make 在同目录树的 `...\win32\others\Build_Tools\Make\bin`。仓库被移动后 `obj/**/*.{mk,d}` 里的绝对路径需 sed 修正（obj 未被 git 跟踪）。
- 官方 EVT 例程参考：`C:\Users\25293\Desktop\aiworkspace\CH32V307EVT\EVT\EXAM`（USBHS 双缓冲/CDC 例程展示了官方推荐的落块模式）。
- 工具链：RISC-V Cross GCC，`-Os`，架构 RV32IMA + C（压缩指令）。
- 烧录/调试配置：`uniconnect.launch`（OpenOCD/wch-link）。

## 目录与分层（自上而下只允许向下依赖）

- `User/` — `main()` 仅空循环，系统状态机（`system_status_t`，main.h）；`config.h` 集中定义各线程优先级与栈大小；中断处理在 `ch32v30x_it.c`。
- `App/` — RT-Thread 线程层：`cdc_uart_thread.c`（CDC↔UART 双向块缓冲零拷贝桥，tx/rx/ctrl/LED 四线程）、`dap_thread.c`（CMSIS-DAP 命令泵，USB 请求/响应队列）、`monitor_thread.c`（按系统状态驱动 LED 动画）、`app_entry.c`（`INIT_APP_EXPORT` 自启动入口）。
- `Bsp/` — 硬件抽象层，App 只准通过 `bsp_led_*` / `bsp_uart_*` / `bsp_usb_*` API 访问硬件，不直接调外设库。`Bsp/bsp_pool.{h,c}` 是通用块缓冲池组件（FREE→LANDING→READY→OUT 状态机，关中断临界区内只做索引运算）；UART RX 与 CDC OUT 两条高速通路的数据缓冲全部走它，块首恒 4 字节对齐。`bsp_config.h` 定义 USB 端点地址（DAP_IN 0x81 / DAP_OUT 0x02 / CDC_IN 0x82 / CDC_OUT 0x04）、VID/PID (0x0D28/0x0204) 及两个块池的块尺寸/池深。
- `Dap/` — CMSIS-DAP 官方源码（DAP.c/SW_DP.c/DAP_queue.c 等），硬件相关全部在 `DAP_config.h`（CPU_CLOCK=144MHz，SWD/JTAG 引脚映射）。
- `CherryUSB/` — vendored 的 CherryUSB v1.51 协议栈（非子模块，可直接改），CH32 移植层在 `port/ch32/ch32hs/`，通过 `osal/` 对接 RT-Thread。
- `CherryRB/` — 环形缓冲区库。
- `RtthreadNano/` — RT-Thread Nano 子模块（github rtthread-nano）。
- `RtthreadNanoPatch/` — 对 Nano 子模块的补丁层（board.c、completion.c、rtconfig.h、libcpu 上下文切换汇编）。include 路径中 Patch 优先于子模块，改 RT-Thread 行为应改这里，不要动子模块。
- `Core/` `Peripheral/` `Startup/` `Ld/` — CH32V30x 厂商驱动库、启动文件、链接脚本，一般不改。

## 启动流程

`Startup → SystemInit() → main()（空循环）→ rtthread_startup() → INIT_APP_EXPORT(app_entry) → 各线程启动`

## 已知坑

## 已知坑

- **[致命] CH32V30x DMA Normal 模式 CNTR=0 静止态狂奔**：块满 TC 后通道处于 EN=1/CNTR=0，该系列（与 STM32 不同！）此时若外设请求挂起会以 65536 计数从旧地址连续狂读/狂写 RAM（已实测涂烂 34KB bss）。对策：RX 用 **CIRCULAR 模式乒乓双块**（DMA 永不停转）；TX 用 **DMAT 门控**（TC4 ISR 第一时间关 DMAT+EN，TC4 中断设为最高抢占优先级 0）。新接任何 DMA 通道必须遵循这两种模式之一。
- **[致命] HPE 硬件栈在高密度中断下弹出错位**（黑匣子实锤：保存的 mstatus 值 0x7800 被恢复进 pc/ra → mcause=2 野跳转）。已在启动代码 `csrw 0x804, 0x3f` 关闭 HPE 硬件栈（bit5），fast 属性处理器兼容。
- **[致命] CherryUSB 驱动总线复位后的陈旧 OUT 完成事件**：DETECT 处理 memset 端点状态后，挂起的 OUT 完成事件继续处理会 `xfer_len` 无符号下溢 → 续传分支把 RX_DMA 写成垃圾地址 → **USB OUT DMA 把数据包写进任意 RAM**（涂烂黑匣子/TCB 的真凶）。已在驱动 PID_OUT 分支加 xfer_buf/xfer_len 守卫。
- **乒乓/池提交账目必须防水**：TC/HT 处理器不得硬编码提交边界（曾把陈旧 pp 段当新数据注入=跨会话"幽灵前缀"）；IDLE 回绕(pos<consumed)必须拆两段提交；队列满/暂存满丢弃时 consumed 必须照样推进（否则下溢巨型 memcpy）。现统一走 `bsp_uart_rx_flush()`。
- **暂存槽容量须 ≥ 2×块**（HT/TC 标志重叠漏计半块后，consumed 滞后会在下次边界并成超长段）。
- **原厂 HardFault 模板会销毁现场**（直接 NVIC_SystemReset）。已换 naked 汇编黑匣子（0x2000E000：mcause/mepc/mtval/ra/sp + 看门狗槽[12..15] + acquire 哨兵[8..11]，写盒前先 `csrw 0x3A0,zero` 兼容 PMP 诱捕）。
- **USB 总线复位后数据通路永久关死**：Windows 开口/错误恢复会发总线复位且之后不再发线控变更。已在 CONFIGURED 事件放行 ctrl 重建（`cdc_uart_event_handler`）。
- **波特率切换必须原子**（ctrl 线程内关中断完成 USART_Init+双向池重建，`bsp_uart_rx_reset` 同时清残留 HT/TC 标志）。
- **线程栈须容纳落在其上的中断 C 调用帧**（-O0 帧很大）：idle 栈 256B 必炸（已设 4096）；`RT_MAIN_THREAD_STACK_SIZE` 不得超过堆容量（board.c 静态 rt_heap），否则主线程创建失败=系统启动残废（现象：USB 不枚举）。
- **start_write 失败必须回滚 `tx_buffer_empty`**，否则方向 A 永久死锁。满 512 块必须发 ZLP——Windows usbser 只在传输边界完成读取（去掉 ZLP 数据滞留驱动不投递）。
- **调试设施**：`g_dbg` 全链路计数器（魔法波特率 1234567 经 CDC IN 返回 84B 快照）；`bsp_watchdog` 金丝雀+OUT DMA 指向校验（monitor 线程）；PMP 诱捕代码骨架已验证可用（保护诱饵区可抓 CPU 写者 PC，DMA 写者不触发）。
- **Windows usbser 主机侧行为**：单笔大写 pending 期间饿死读（测试须分块写+低波特率加 10ms 节奏）；数据完整性与设备无关。
- **性能边界与残余问题（2026-09-30 实测）**：RX 已是 8KB 循环环形+贪心收割+合并大传输(≤4KB)。≤1.5M 高可靠（多轮 64KB×16 长锤 bit-perfect）；2M+ 偶发丢 512B 整块（概率性，字节总账证明设备 USB 层与主机写入分毫不差——out_bytes=tx_bytes=in_bytes 对账全平，丢失发生在 USB OUT 软件处理窗口，主嫌疑：ch32hs 驱动 PID_OUT 的 TOG_OK 校验丢弃已被硬件 ACK 的包、或 out_done 回调的重复/缺失微窗口）。下一步排查方向：给 PID_OUT 的 TOG_OK 丢弃路径加计数器；用 USBPcap 抓主机侧实际包序；测试脚本改累计流比对（消除应用层 purge 丢尾干扰）。
- **测试方法论**：Windows usbser 应用层读满即停+下轮 purge 会丢迟到尾巴（测试须累计流比对）；单笔大写饿死读（分块写+低速档 10ms 节奏）；每 512 倍数传输必须 ZLP 否则主机不投递。
- **子模块异常**：`CherryRB` 在 git index 中是 gitlink 但 `.gitmodules` 无对应条目。
- USB 时钟：USBPHY 48MHz，PLL 以 HSE 为参考；主频已从早期版本调到 144MHz，勿按旧文档的 132MHz 理解。

## 约定

- 注释、commit message、文档用中文；commit 风格见 `git log`（短中文描述）。
- 命名：厂商库 `ch32v30x_` 前缀，BSP 层 `bsp_` 前缀（`Bsp/inc` + `Bsp/src`），RT-Thread 用 `rt_` 前缀，应用层小写下划线。
- 调试输出用 `rt_kprintf()`；USB 协议栈日志走 `CONFIG_USB_PRINTF()` 宏。
- 状态 LED 共 5 个（SYS/SWD/COM/CAN1/CAN2），统一通过 `bsp_led_ctrl/toggle` 操作，高电平为灭。
- 新线程的优先级/栈大小必须加到 `User/config.h`，不要在源文件里写魔数。

## 当前进度（2026-09）

CDC↔UART 双向块缓冲零拷贝已落地（待上板回归：枚举/回环大流量/逐字符/线控切换/背压恢复）；DAP 配置进行中（暂存状态）；SWD 线程为空实现；CAN 未开始。CherryRB 已退出数据通路（目录保留、代码未引用），后续新数据通路一律用 `bsp_pool`。
