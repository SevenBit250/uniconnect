# AGENTS.md

UniConnect：基于 CH32V305CCT6（RISC-V，主频 144MHz）的 CMSIS-DAP 调试探针固件，目标功能为 SWD 调试 + CDC 虚拟串口 + 双路 CAN。运行 RT-Thread Nano。当前状态：**四大功能全部落地并真机验证**——CDC↔UART（9M 全档位零丢失）、CAN-USB 桥、DAP bulk v2、SWD 目标调试（STM32F103 全枚举）；MS OS 2.0 免驱已修通。

## 构建与调试

- IDE 为 MounRiver Studio (MRS) 2.3.0（Eclipse CDT 托管构建），项目配置在 `.project` / `.cproject`，勿手改 `obj/` 下自动生成的 makefile。
- 命令行构建（推荐）：仓库根 `bash gen_makefile.sh` 一次性生成 `obj/GNUmakefile`（继承 MRS makefile + 工具链 PATH 注入 + build/ 产物收集）和根 `Makefile` 转发层，之后根目录直接 `make`（产物 elf/bin/hex/map 收集到 `build/`）、`make flash`（openocd + wch-link 烧录）、`make clean`。make 本体不在 PATH 时先 `export PATH="/c/MounRiver/MounRiver_Studio2/resources/app/resources/win32/others/Build_Tools/Make/bin:$PATH"`。
- 直接用 MRS 原始 makefile：`cd obj && make all`（必须 `make all`，裸 `make` 行为不对；工具链需自行注入 PATH）。
- 不依赖 IDE 的本地构建：工具链在 `C:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\components\WCH\Toolchain\RISC-V Embedded GCC\bin`，make 在同目录树的 `...\win32\others\Build_Tools\Make\bin`，openocd 及 `wch-riscv.cfg` 在 `...\win32\components\WCH\OpenOCD\OpenOCD\bin`。仓库被移动后重跑 `gen_makefile.sh` 即自愈 GNUmakefile 里的 Link.ld 绝对路径（`obj/**/*.d` 里的绝对路径仍会重新生成）。
- 官方 EVT 例程参考：`C:\Users\25293\Desktop\aiworkspace\CH32V307EVT\EVT\EXAM`（USBHS 双缓冲/CDC 例程展示了官方推荐的落块模式）。
- 工具链：RISC-V Cross GCC，`-Os`，架构 RV32IMA + C（压缩指令）。
- 烧录/调试配置：`uniconnect.launch`（OpenOCD/wch-link）。

## 目录与分层（自上而下只允许向下依赖）

- `User/` — `main()` 仅空循环，系统状态机（`system_status_t`，main.h）；`config.h` 集中定义各线程优先级与栈大小；中断处理在 `ch32v30x_it.c`。
- `App/` — RT-Thread 线程层：`cdc_uart_thread.c`（CDC↔UART 双向块缓冲零拷贝桥，tx/rx/ctrl 三线程）、`dap_thread.c`（CMSIS-DAP 命令泵，USB 请求/响应队列）、`monitor_thread.c`（统一 LED 管理 + 看门狗：SYS 心跳/流水、COM/CAN1/CAN2 吞吐活动灯按 g_dbg 计数器 10ms 轮询最小亮 30ms、线控确认双闪，数据路径零 LED 代码）。
- `Bsp/` — 硬件抽象层，App 只准通过 `bsp_*` API 访问硬件，不直接调外设库；**依赖方向严格单向向下**（2026-10-01 解耦重构：曾数据面混在 bsp_usb.c 且 extern App 回调/替 App 初始化信号量，现已各归其主）。`device/{can,led,uart,usb}/` 各含外设驱动 `.c/.h`；`components/block_buffer_pool/`（`bsp_pool`，通用块缓冲池组件，FREE→LANDING→READY→OUT 状态机，关中断临界区内只做索引运算；UART RX 与 CDC OUT 两条高速通路的数据缓冲全部走它，块首恒 4 字节对齐）、`components/watchdog/`（金丝雀+OUT DMA 指向注册制监护+黑匣子）。`bsp_config.h` 与伞头 `bsp.h` 留在 `Bsp/` 根，集中定义 USB 端点地址（DAP_IN 0x81 / DAP_OUT 0x02 / CDC_IN 0x82 / CDC_OUT 0x04）、VID/PID (0x0D28/0x0218) 及两个块池的块尺寸/池深。
- **USB 数据面归属（2026-10-01 解耦）**：`Bsp/device/usb/bsp_usb.c` 只做设备层（描述符表=设备级复合布局、按描述符顺序注册接口端点、事件分发钩子）；CDC/CAN/DAP 的数据面分别在 `App/cdc_uart_thread.c`、`App/can_bridge_thread.c`、`App/dap_thread.c`，各自在 INIT_COMPONENT 阶段填充句柄（ep_addr/ep_cb/cfg_cb 钩子，早于 bsp_usb 层 INIT_ENV 的 usbd_initialize）完成向下注册；CONFIGURED 事件经钩子触发各数据面自愈（重开端点/重挂 OUT 池/挂首包）。新增 Bsp 源文件须放入对应 device/components 子目录并在 obj 下配 subdir.mk（可参考 obj/Bsp/device/can/subdir.mk 模板，或让 MRS 重新生成）。
- `Dap/` — CMSIS-DAP 官方源码（DAP.c/SW_DP.c/DAP_queue.c 等），硬件相关全部在 `DAP_config.h`（CPU_CLOCK=144MHz，SWD/JTAG 引脚映射）。
- `CherryUSB/` — vendored 的 CherryUSB v1.51 协议栈（非子模块，可直接改），CH32 移植层在 `port/ch32/ch32hs/`，通过 `osal/` 对接 RT-Thread。
- `RtthreadNano/` — RT-Thread Nano 子模块（github rtthread-nano）。
- `RtthreadNanoPatch/` — 对 Nano 子模块的补丁层（board.c、completion.c、rtconfig.h、libcpu 上下文切换汇编）。include 路径中 Patch 优先于子模块，改 RT-Thread 行为应改这里，不要动子模块。
- `Core/` `Peripheral/` `Startup/` `Ld/` — CH32V30x 厂商驱动库、启动文件、链接脚本，一般不改。

## 启动流程

`Startup → SystemInit() → main()（空循环）→ rtthread_startup() → INIT_APP_EXPORT(app_entry) → 各线程启动`

## 已知坑

- **Windows 原生 make 的 `$(PATH)` 恒为分号形式**：Git Bash 启动原生 make.exe 时 MSYS 会把 PATH 环境变量自动转成 `C:\...;C:\...` 形式，makefile 里 `export PATH := /c/xxx:$(PATH)` 会拼出冒号+分号混合的废 PATH（bundled sh 报工具 not found，且 `/c/` 挂载形式它也不认）。正确做法见 `gen_makefile.sh` 生成的 GNUmakefile：路径用 `C:/` 正斜杠形式 + `$(findstring ;,$(PATH))` 探测分隔符再拼接。

- **[致命] CH32V30x DMA Normal 模式 CNTR=0 静止态狂奔**：块满 TC 后通道处于 EN=1/CNTR=0，该系列（与 STM32 不同！）此时若外设请求挂起会以 65536 计数从旧地址连续狂读/狂写 RAM（已实测涂烂 34KB bss）。对策：RX 用 **CIRCULAR 模式乒乓双块**（DMA 永不停转）；TX 用 **DMAT 门控**（TC4 ISR 第一时间关 DMAT+EN，TC4 中断设为最高抢占优先级 0）。新接任何 DMA 通道必须遵循这两种模式之一。
- **[致命] HPE 硬件栈在高密度中断下弹出错位**（黑匣子实锤：保存的 mstatus 值 0x7800 被恢复进 pc/ra → mcause=2 野跳转）。已在启动代码 `csrw 0x804, 0x3f` 关闭 HPE 硬件栈（bit5），fast 属性处理器兼容。
- **[致命] CherryUSB 驱动总线复位后的陈旧 OUT 完成事件**：DETECT 处理 memset 端点状态后，挂起的 OUT 完成事件继续处理会 `xfer_len` 无符号下溢 → 续传分支把 RX_DMA 写成垃圾地址 → **USB OUT DMA 把数据包写进任意 RAM**（涂烂黑匣子/TCB 的真凶）。已在驱动 PID_OUT 分支加 xfer_buf/xfer_len 守卫。
- **乒乓/池提交账目必须防水**：TC/HT 处理器不得硬编码提交边界（曾把陈旧 pp 段当新数据注入=跨会话"幽灵前缀"）；IDLE 回绕(pos<consumed)必须拆两段提交；**partial 暂存失败必须无损延迟重试（不推进账目，数据留环内下次 drain 重推，期间新数据自然合并成满块）——旧实现直接丢弃曾致混合负载丢字节（submit_drop 14次/300s），且延迟期间若环回绕覆写源数据须丢弃推进防幽灵数据**。暂存槽 4 个。现统一走 `bsp_uart_rx_flush()`。
- **暂存槽容量须 ≥ 2×块**（HT/TC 标志重叠漏计半块后，consumed 滞后会在下次边界并成超长段）。
- **原厂 HardFault 模板会销毁现场**（直接 NVIC_SystemReset）。已换 naked 汇编黑匣子（0x2000E000：mcause/mepc/mtval/ra/sp + 看门狗槽[12..15] + acquire 哨兵[8..11]，写盒前先 `csrw 0x3A0,zero` 兼容 PMP 诱捕）。
- **USB 总线复位后数据通路永久关死**：Windows 开口/错误恢复会发总线复位且之后不再发线控变更。已在 CONFIGURED 事件放行 ctrl 重建（`cdc_uart_event_handler`）。
- **波特率切换必须原子**（ctrl 线程内关中断完成 USART_Init+双向池重建，`bsp_uart_rx_reset` 同时清残留 HT/TC 标志）。
- **线程栈须容纳落在其上的中断 C 调用帧**（-O0 帧很大）：idle 栈 256B 必炸（已设 4096）；`RT_MAIN_THREAD_STACK_SIZE` 不得超过堆容量（board.c 静态 rt_heap），否则主线程创建失败=系统启动残废（现象：USB 不枚举）。
- **start_write 失败必须回滚 `tx_buffer_empty`**，否则方向 A 永久死锁。满 512 块必须发 ZLP——Windows usbser 只在传输边界完成读取（去掉 ZLP 数据滞留驱动不投递）。
- **调试设施**：`g_dbg` 全链路计数器（魔法波特率 1234567 经 CDC IN 返回 84B 快照）；`bsp_watchdog` 金丝雀+OUT DMA 注册制监护（monitor 线程）；`tools/stack_watermark.py` 线程栈水位检测（'#'预填单调保持，事后扫描等价实时，改栈配置后必跑）；PMP 诱捕代码骨架已验证可用（保护诱饵区可抓 CPU 写者 PC，DMA 写者不触发）。**全部测试/验证/调试工具的说明见 `tools/README.md`**。
- **Windows usbser 主机侧行为**：单笔大写 pending 期间饿死读（测试须分块写+低波特率加 10ms 节奏）；数据完整性与设备无关。
- **性能边界（2026-09-29 RM 精读后突破）**：**921600–9M（UART 理论极限）全档位零丢失**（256KB×5 满载锤 + 5M 1MB×10 / 9M 1MB×8 soak + 回归 11/11 全 PASS，9M 实测 833KB/s 线速）。关键修复：RM `RB_UC_INT_BUSY`（USB_CTRL bit3，CherryUSB/EVT 本就置位）语义是"TRANSFER 标志置位期间 SIE 自动 NAK 所有新事务"——但 ISR 入口提前清标志使保护失效，处理当前 OUT 包期间下一个背靠背包被 ACK 进**未换出的旧 RX 缓冲**覆写正在消费的数据（HS 包间隔 µs 级才触发，FS 300µs+ 不触发，故此前误判为硅级竞态）。正确做法：**INT_ST 先快照（NAK 重试会刷新它），TRANSFER 标志清除必须放在端点重武装之后（ISR 体末尾）**，见 `usb_dc_usbhs.c` USBD_IRQHandler。此前"4M/5M 硅级极限"结论作废。usbser 写侧全量到达（WriteFile 无短写无异常，分层排除法完整记录于提交历史）。
- **测试方法论**：Windows usbser 应用层读满即停+下轮 purge 会丢迟到尾巴（测试须累计流比对）；单笔大写饿死读（分块写+低速档 10ms 节奏）；每 512 倍数传输必须 ZLP 否则主机不投递。
- **子模块异常（已解决）**：`CherryRB` 曾是 git index 中无 `.gitmodules` 条目的 gitlink，2026-10-01 确认零引用后整体移除（`git rm -f` + obj 下两个 mk 剔除 + .cproject include 路径清理）。
- USB 时钟：USBPHY 48MHz，PLL 以 HSE 为参考；主频已从早期版本调到 144MHz，勿按旧文档的 132MHz 理解。
- **USBHS 真实基址是 0x40023400（非 0x40022000！）**：watchdog 曾用错基址读 UEP4_RX_DMA 得垃圾值，复合模式 CDC 武装后必误杀芯片（表现：设备整个从总线消失）。EP4_RX_DMA = base+0x2C。
- **MS OS 2.0 描述符集长度账三处同步（2026-10-01 扩双子集再踩两坑）**：同一总长度存在**集合头 wTotalLength 与 BOS 平台能力 wMSOSDescriptorSetTotalLength 两份**，改一处漏一处 → Windows 判废 → 父设备 Error 无子接口（实例 ID 无序列号是其签名）；另手抄第二功能子集曾丢 2 字节（GUID 尾部 'AA' 丢一 A，实际 328≠声明 330）。**改 MSOS 集合必须跑 `python tools/msos_validate.py`**（机械校验平铺/长度同步/类型值，勿肉眼对字节）。
- **DAP 厂商接口必须排在 CAN 之前（接口 2）**：OpenOCD cmsis-dap bulk 后端取**第一个**厂商类 bulk 双端点接口（此 build 连接口字符串都不严格校验），CAN 前置会抢走接口 → DAP_Info 发给 CAN 桥 → CMD_INFO failed 而设备端毫无感知（EP 上零事务是判别特征）。现行布局：CDC=0/1、DAP=2（字符串 "CMSIS-DAP"）、CAN=3（字符串 "UniConnect CAN Bridge"），`tools/can_usb_test.py` 的 IF_NUM 须同步。
- **.bss 不得侵入 0x2000E000 起的调试基建**：PMP 诱饵区（0xE000，2KB）/金丝雀（0xE800）/崩溃黑匣子（0xF000）为固定地址裸访问，.bss 增长盖住后 DAP 队列写入涂改金丝雀→看门狗处决。**Link.ld 已加 `ASSERT(_ebss < 0x2000E000)` 机械防回退**；DAP_PACKET_COUNT 因此 8→2（省 6KB），性能优化腾出内存后再加回。
- **USBHS RX_DMA 寄存器回读丢 bit29**（RAM 基址 0x20000000 不存储，硬件隐含，CDC/CAN/DAP 三端点实测一致）：任何拿该寄存器与完整指针比较的代码必须**两侧同掩 `& 0x1FFFFFFF`**——watchdog 的 OUT DMA 指向校验曾因截断值比完整指针，CDC 一武装就误杀 monitor 线程（黑匣子 kind=2 实锤）。
- **栈上字节数组偏移处做 uint32 直写=未对齐地雷**：`uint32_t *w = (uint32_t *)&rsp[2]` 类代码对齐性取决于当次栈布局，昨天的布局碰巧无害、今天漂移即触发对齐陷阱 HardFault 杀死线程（can_bridge GET_STATUS 首例，现象=该命令响应永不到达而线程整体失踪）。多字节装配一律 memcpy 或逐字节。
- **openocd 观测技巧**：`halt + mdw 符号地址` 可直接读运行固件的任意 RAM/寄存器（计数器/黑匣子/USBHS 现场定位神器，配合 map 文件查地址）；注意 openocd 的 `reset run` 不一定触发 Windows 重枚举，halt/resume 循环反而会扰动总线触发重枚举。
- **复合模式接口注册顺序必须与描述符一致**：usbd_add_interface 按注册序分配 intf_num；CAN 曾前置抢 0 号，Windows 给 CDC 接口 0 的类请求被路由到无处理器的 CAN 接口 → STALL → usbser 打开报错 31（"设备没有发挥作用"）。
- **WCH-Link DAP模式(WinUSB)在本机正常的真相: 不走MS OS, 走WCH签名INF驱动包**。WCH-LinkUtility/MRS安装时装入 WCHLinkWDM.INF(oem54.inf, WCH的EV证书签名), 按硬件ID `USB\VID_1A86&PID_8012&MI_00` 精确匹配后绑WinUSB。**WCH自家产品都不依赖MS OS 2.0免驱**。UniConnect量产路线决策: CAN接口改第二CDC(usbser全平台inbox零驱动, 二进制协议不变), DAP走HID v1+bulk v2双接口; MS OS描述符保留但非依赖项。
- **MS OS 2.0 免驱失败的根因（2026-10-01 已修）**：描述符集两个类型字段违反 MS 规范表 9——集合头 `wDescriptorType` 误为 0x0006（=Model ID，规范固定 22B，我们 10B，长度校验失败）、功能子集头误为 0x0008（=Vendor Revision，未定义用途）。正确值：**集合头 0x0000 / 配置子集 0x01 / 功能子集 0x02 / 兼容ID 0x03 / 注册表属性 0x04**（见 CherryUSB `common/usb_def.h` 288-295 行与微软规范 Table 9）。Windows 按 Model ID 解析失败后整集判废 → 枚举在 BOS 后中止（SET_CONFIG 不发、字符串不读、usbccgp 1ms 内 STATUS_NO_SUCH_DEVICE）——与 ETW 观测完全吻合。此前"描述符内容已排除"结论错误（当时只核对了长度/UUID/GUID，漏了类型值）；KB5124008 补丁时间线是巧合。历史排查中 EP0 多包 bug、ISR 竞态、watchdog 基址、注册顺序、stale object 等修复均为真实缺陷，保留。**真机验证 PASS（2026-10-01）**：接口 2 `Service=WINUSB`、兼容 ID `USB\MS_COMP_WINUSB`，双路 CAN 1000 帧回环全 PASS（`tools/can_usb_test.py`，PID 0x0218）。调试期曾用 PID 0x0220 隔离 Windows 设备缓存，验证后已恢复生产 PID。
- CAN（2026-09-29 双路驱动起来）：**CH32V30x CAN 为 bxCAN 架构，RM 第 24 章无 DMA 请求线**——用 FIFO0 FMP 中断收帧 + TX 空中断续装邮箱（帧环 SPSC），每帧 ISR 约 4 次寄存器读，1M 波特率满载 ~8k 帧/s 时 ISR 占用 <2%。**WCH 库 `CAN_Init` 成功返回 1 失败返回 0（STM32 风格），不能按真值判断**（曾因 `if(ret)` 把成功当失败在 CAN1 后早退）。库内含特定封装的出厂自检路径（APB1 复位脉冲 + 4Mbps 静默回环自测帧）。引脚：CAN1=PB8/PB9（Remap1，PD0/PD1 在 48 脚不存在）、CAN2=PB12/PB13（固定）。APB1=72MHz（PPRE1_DIV2），1M 波特率 = P4 × 18tq（SJW1+BS1_13tq+BS2_4tq，采样点 77.8%）。`CAN_FilterInit` 只收一个参数（无 CANx，bank 序号决定归属，`CAN_SlaveStartBank` 切分 28 组 bank）。回环自测模式 `CAN_Mode_Silent_LoopBack` 不驱动总线。

## 开发铁律

- **[铁律] 遇到顽固 bug 必须调用 `engineering-method` skill（用户级 `~/.zcode/skills/engineering-method/`），不得凭惯性继续排查**。触发线（任一）：同一故障第 2 个假说死亡；开始怀疑 OS 补丁/硅级 bug/竞态/"玄学"等外因；"已排除"的结论被反复拿出来重审；修了真实缺陷但原始症状纹丝不动。触发后停止生产新假说，按 skill 的四条增量纪律执行：排除结论必须带覆盖清单、外因假说须机制或对照实验、修复≠根因（正交缺陷陷阱）、真值三源对照（规范表格/vendored 常量/真机抓包）。MS OS 2.0 免驱排查（根因 2 字节、疑点十几个、耗时数周）是其锚点案例，翻车详情见 skill 的 references。

## 约定

- 注释、commit message、文档用中文；commit 风格见 `git log`（短中文描述）。
- 命名：厂商库 `ch32v30x_` 前缀，BSP 层 `bsp_` 前缀（`Bsp/inc` + `Bsp/src`），RT-Thread 用 `rt_` 前缀，应用层小写下划线。
- 调试输出用 `rt_kprintf()`；USB 协议栈日志走 `CONFIG_USB_PRINTF()` 宏。
- 状态 LED 共 5 个（SYS/SWD/COM/CAN1/CAN2），统一通过 `bsp_led_ctrl/toggle` 操作，高电平为灭。
- 新线程的优先级/栈大小必须加到 `User/config.h`，不要在源文件里写魔数。

## 当前进度（2026-10）

- CDC↔UART 双向块缓冲零拷贝：已落地，921600–9M 全档位零丢失（回归 11/11 PASS）。
- CAN 双路驱动 + CAN-USB 桥（WinUSB 复用双路）：已落地，双路 1000 帧回环全 PASS。
- MS OS 2.0 免驱：根因（描述符类型字段违反规范表 9）已修，真机免驱绑定 WinUSB 验证 PASS。
- **DAP bulk v2 通路已打通（2026-10-01）**：接口 2（"CMSIS-DAP"），OpenOCD 不接目标板可读完整 link 信息（v2/SWD/Atomic/FW 2.1.1/引脚状态），与 CAN 回归双绿。
- DAP/SWD：**全链路已打通（2026-10-01 真机验证）**：SWD 引脚引擎 `Bsp/device/dap/bsp_dap.{c,h}`（SWCLK=PB3/SWDIO=PB5，GPIO 位拍为默认引擎 + `BSP_DAP_USE_SPI` 宏切换 SPI 单线半双工兼容后端供将来加速）；对 STM32F103C8T6 真机完成 DP/AP 全枚举（DPIDR=0x1BA01477，Cortex-M3 r1p1，6 断点 4 观察点）。注意 PB3/PB4 出厂复用为 JTDO/NJTRST，bsp_dap_init 里 SWJ_Disable 释放（SDI 烧录口实测不受影响）。WCH 版 openocd 的 target/*.cfg 有损坏的脚本搜索前缀（指向不存在的 `C:/MounRiver/MounRiver_Studio/...`），勿用 `-f target/stm32f1x.cfg`，用手工 DAP 声明：`openocd -c "adapter driver cmsis-dap" -c "transport select swd" -c "adapter speed 1000" -c "swd newdap stm32 dp -expected-id 0x1ba01477" -c "dap create stm32.dap -chain-position stm32.dp" -c "target create stm32.cpu cortex_m -dap stm32.dap" -c init`。
- 下一步：SWD 深化（halt/读写内存/闪存算法）、DAP 加速引擎（SPI 后端）、CAN 接口改第二 CDC 量产路线评估。
