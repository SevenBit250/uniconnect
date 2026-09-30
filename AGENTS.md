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

- **[致命] CH32V30x DMA Normal 模式 CNTR=0 静止态狂奔**：块满 TC 后通道处于 EN=1/CNTR=0，该系列（与 STM32 不同！）此时若外设请求挂起会以 65536 计数从旧地址连续狂读/狂写 RAM（已实测从块首涂烂 34KB bss，是所有"随机崩溃/野指针/复位"的第一因）。对策：RX 用 **CIRCULAR 模式乒乓双块**（`bsp_uart.c`，DMA 永不停转）；TX 用 **DMAT 门控**（TC4 ISR 第一时间 `USART1->CTLR3 &= ~USART_DMAReq_Tx`，`write_by_dma` 最后一步再开）。新接任何 DMA 通道必须遵循这两种模式之一。
- **原厂 HardFault 模板会销毁现场**：`ch32v30x_it.c` 原版直接 `NVIC_SystemReset()`。已换成 naked 汇编黑匣子（mcause/mepc/mtval/ra/sp 写 0x2000E000 后死循环），配合 OpenOCD `mdw` 取证。崩溃排查先读黑匣子再 addr2line。
- **USB 总线复位后数据通路永久关死**：Windows 开串口/错误恢复会发总线复位，`allow_transfer` 关闭后主机不会再发线控变更。已在 CONFIGURED 事件中释放 `lincoding_sem` 让 ctrl 线程重建（`cdc_uart_event_handler`）。
- **线程栈必须容纳落在其上的中断 C 调用帧**（WCH-Interrupt-fast 的帧压在被中断线程栈上，-O0 帧很大）：idle 栈默认 256B 必炸（rtconfig.h 已设 4096）；ctrl 线程 512B 会爆栈。`RT_MAIN_THREAD_STACK_SIZE` 不能超过堆（`board.c` 的 rt_heap 静态数组）能分配的量，否则主线程创建失败=整个系统启动残废（现象：USB 不枚举、计数器全零）。
- **start_write 失败必须回滚 `tx_buffer_empty`**，否则无 bulk_in 回调、方向 A 永久死锁。
- **调试计数器**（`g_dbg`，bsp_pool.h）+ 魔法波特率 1234567 读快照（经 CDC IN 返回 84B）；0x2000D000 起为调试日志区。
- **子模块异常**：`CherryRB` 在 git index 中是 gitlink 但 `.gitmodules` 无对应条目，`git submodule status` 直接报错。
- **USBHS DMA 4 字节对齐**：高速通路用乒乓块/块池（块首恒对齐）；CherryUSB ch32hs 驱动的对齐检查已恢复激活，非对齐指针立即返回 -3。
- **遗留问题（2026-09-30）**：921600 满速大流量（256KB）跑完后约 126B 尾巴会延迟到下一个事务投递（重现：tools/cdc_loopback_test.py 测试5 后立即小包测试）。怀疑 seq 过期丢弃/队列满路径的时序；115200 全场景与 921600 数据长度均正确。
- Windows usbser 在单线程大写阻塞期间停止取 IN（设备池满丢字节属无流控固有行为），测试须用并发读写（tools 脚本已是）。
- USB 时钟：USBPHY 48MHz，PLL 以 HSE 为参考；主频已从早期版本调到 144MHz，勿按旧文档的 132MHz 理解。

## 约定

- 注释、commit message、文档用中文；commit 风格见 `git log`（短中文描述）。
- 命名：厂商库 `ch32v30x_` 前缀，BSP 层 `bsp_` 前缀（`Bsp/inc` + `Bsp/src`），RT-Thread 用 `rt_` 前缀，应用层小写下划线。
- 调试输出用 `rt_kprintf()`；USB 协议栈日志走 `CONFIG_USB_PRINTF()` 宏。
- 状态 LED 共 5 个（SYS/SWD/COM/CAN1/CAN2），统一通过 `bsp_led_ctrl/toggle` 操作，高电平为灭。
- 新线程的优先级/栈大小必须加到 `User/config.h`，不要在源文件里写魔数。

## 当前进度（2026-09）

CDC↔UART 双向块缓冲零拷贝已落地（待上板回归：枚举/回环大流量/逐字符/线控切换/背压恢复）；DAP 配置进行中（暂存状态）；SWD 线程为空实现；CAN 未开始。CherryRB 已退出数据通路（目录保留、代码未引用），后续新数据通路一律用 `bsp_pool`。
