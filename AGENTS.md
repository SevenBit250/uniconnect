# AGENTS.md

UniConnect：基于 CH32V305CCT6（RISC-V，主频 144MHz）的 CMSIS-DAP 调试探针固件，目标功能为 SWD 调试 + CDC 虚拟串口 + 双路 CAN。运行 RT-Thread Nano。当前状态：**四大功能全部落地并真机验证**——CDC↔UART（9M 全档位零丢失）、CAN-USB 桥、DAP bulk v2、SWD 目标调试（STM32F103 全枚举）；MS OS 2.0 免驱已修通。

## 构建与调试

- 构建系统：**CMake + Ninja**（2026-10-03 从 MRS/make 迁移，MRS 已退役；迁移时做过符号级等价验证：APP 418/418、boot 191/191 符号大小与 make 基线全同）。仓库根：
  - `cmake -B build -G Ninja`（一次配置；toolchain 自动挂载，ninja 经 `pip install ninja`）
  - `cmake --build build`——app+boot 全建，产物 `build/uniconnect.{elf,bin,hex,map}` 与 `build/boot.{elf,bin,hex,map}`（`fw_update.py` 消费 bin、`stack_watermark.py` 消费 map，路径约定不变）
  - `cmake --build build --target uniconnect.elf` / `boot.elf`——单建
  - `cmake --build build --target flash_app / flash_boot / flash_all`——openocd+wch-link 烧录（**烧后若 USB 不重枚举需物理重插 POR**；`flash erase_address` 禁用=整片擦）
- 构建类型：`-DCMAKE_BUILD_TYPE=Debug`（默认，`-O0 -g`，对齐全部历史性能验证位）/ `Release`（`-Os`）。**boot 的 Debug 特例映射 `-O1`**（-O0 实测 32512B 溢 20KB 分区 12KB，链接面已是最小集 cherryusb_core+hid，见 boot/CMakeLists.txt 注释）。**切换 Release 属独立流程：需全回归（CDC/CAN/DAP/fw_update）+ stack_watermark 栈水位**。
- 工具链与烧录路径默认取 MRS 安装位置，可用环境变量 `TOOLCHAIN_BIN` / `OPENOCD_BIN` 覆盖（见 `cmake/toolchain.cmake`、`cmake/flash.cmake`）；工具链本体在 `C:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\components\WCH\Toolchain\RISC-V Embedded GCC\bin`，openocd 及 `wch-riscv.cfg` 在 `...\win32\components\WCH\OpenOCD\OpenOCD\bin`。
- CMake 架构：各模块（CherryUSB 类驱动/端口、厂商库 hal_full/hal_min、RT-Thread、boot_shared）一律 **INTERFACE 源集**——源码随消费者目标编译，优化等级与宏（如 `CONFIG_USB_BAREMETAL`）按固件目标生效；`usb_dc_usbhs.c` 由 boot 直挂源+私有宏实现同文件双编译。
- 旧 make 体系（gen_makefile.sh/根 Makefile/obj/GNUmakefile/subdir.mk/.cproject）已随迁移删除，MRS 工程文件不再维护。
- 官方 EVT 例程参考：`C:\Users\25293\Desktop\aiworkspace\CH32V307EVT\EVT\EXAM`（USBHS 双缓冲/CDC 例程展示了官方推荐的落块模式）。
- 工具链：RISC-V Cross GCC，`-Os`，架构 RV32IMA + C（压缩指令）。
- 烧录/调试配置：`uniconnect.launch`（OpenOCD/wch-link）。

## 目录与分层（自上而下只允许向下依赖；app/boot/common/third_party 四分法）

- `app/` — **APP 固件工程**（RT-Thread 全功能，flash@0x5000）：`Application/` 线程层（`cdc_uart_thread.c` CDC↔UART 双向块缓冲零拷贝桥 tx/rx/ctrl 三线程、`dap_thread.c` CMSIS-DAP 命令泵、`can_bridge_thread.c` CAN 桥、`monitor_thread.c` 统一 LED 管理+看门狗）；`Bsp/` 硬件抽象层（App 只准通过 `bsp_*` API 访问硬件，不直接调外设库，**依赖方向严格单向向下**；`device/{can,led,uart,usb,dap}/` 外设驱动、`components/block_buffer_pool/`（`bsp_pool` 块缓冲池，FREE→LANDING→READY→OUT 状态机）、`components/watchdog/`（金丝雀+OUT DMA 注册制监护+黑匣子）；`bsp_config.h`/伞头 `bsp.h` 在 `Bsp/` 根，集中定义 USB 端点地址与 VID/PID）；`Dap/` CMSIS-DAP 官方源码（硬件相关全在 `DAP_config.h`，CPU_CLOCK=144MHz）；`User/`（`main()` 空循环+`.appsig` 签名字、`config.h` 集中线程优先级/栈大小、`ch32v30x_it.c` 中断、`debug.c`）；`app.ld` 链接脚本（0x5000/232K+BOINIT 区+RAM 52K+`.appsig` 段）。
- `boot/` — **bootloader 固件工程**（裸机无 RTOS，flash@0x0/20KB）：`User/`（`main.c` 启动决策+跳转、`bootproto.c` 升级协议、`flash_io.c` 快编程 0x08 别名封装、`usb_glue.c` 单 HID 描述符+裸机泵、`boot_it.c` 中断兜底、`system_ch32v30x.c` **96MHz 有意分叉**——flash 擦写时钟红线）；`boot.ld`（20K+BOINIT+ASSERT 溢出兜底）。
- `common/` — **app+boot 公共层**：`pin.h`（**全板引脚单一事实源**：LED×5/USART1/CAN×2/SWD 分配，boot 与 APP 的 bsp 一律引用，不得私定义；DAP_config.h 位拍宏保持独立实现但分配以此为准）；`Core/`、`Peripheral/`（厂商库；APP 编全集 26 个外设 .c=`ch32_hal_full`，boot 编最小集 rcc/gpio/flash/misc=`ch32_hal_min`，CMake 接口源集区分）；`Startup/`（`startup_ch32v30x_D8C.S`=APP 用、`_D8.S`=boot 用）；`boot_shared.h`（BOINIT 共享区/旗标页/`APP_SIG_MAGIC` 单一事实源，改布局需同步 app.ld/boot.ld/本头三方）；`system_ch32v30x.h`（.c 两侧有意分叉：APP 144M/boot 96M，头共享）。
- `third_party/` — `CherryUSB/`（vendored v1.51 非子模块可直接改；CH32 移植层在 `port/ch32/ch32hs/`，经 `osal/usb_osal_rtthread.c` 对接 RT-Thread；模块化 CMake 接口源集在此目录）；`RtthreadNano/`（**vendor 化收编**，2026-10-03 由 git 子模块转普通目录，固定 v3.1.5，防上游变动）；`RtthreadNanoPatch/`（Nano 补丁层：board.c、completion.c、rtconfig.h、libcpu 上下文切换汇编；`rtthread` CMake 接口源集在此定义——只编 Nano `rt-thread/src` 15 个内核 .c + Patch 副本，改 RT-Thread 行为改这里）。
- `cmake/` — `toolchain.cmake`（riscv-none-embed 工具链，`TOOLCHAIN_BIN` 可覆盖）+ `flash.cmake`（flash_app/flash_boot/flash_all）。
- `tools/`/`docs/` — 测试诊断工具（见 `tools/README.md`）与设计文档；产物目录 `build/`（gitignored）。
- **USB 数据面归属（2026-10-01 解耦）**：`app/Bsp/device/usb/bsp_usb.c` 只做设备层（描述符表=设备级复合布局、按描述符顺序注册接口端点、事件分发钩子）；CDC/CAN/DAP 的数据面分别在 `app/Application/{cdc_uart_thread,can_bridge_thread,dap_thread}.c`，各自在 INIT_COMPONENT 阶段填充句柄（ep_addr/ep_cb/cfg_cb 钩子，早于 bsp_usb 层 INIT_ENV 的 usbd_initialize）完成向下注册；CONFIGURED 事件经钩子触发各数据面自愈（重开端点/重挂 OUT 池/挂首包）。新增 Bsp 源文件放入对应 device/components 子目录后，**还需加入 `app/CMakeLists.txt` 的 add_executable 源清单**。

## 启动流程

`Startup → SystemInit() → main()（空循环）→ rtthread_startup() → INIT_APP_EXPORT(app_entry) → 各线程启动`

## 已知坑


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
- **调试设施**：`g_dbg` 全链路计数器（魔法波特率 1234567 经 CDC IN 返回 84B 快照）；`bsp_watchdog` 金丝雀+OUT DMA 注册制监护（monitor 线程）；`tools/stack_watermark.py` 线程栈水位检测（'#'预填单调保持，事后扫描等价实时，改栈配置后必跑；多构建目录须 `--map build-rel/uniconnect.map` 指定与设备固件同构建的 map，错配=地址全错读数无效——Release 固件+Debug map 曾误报 100%）；PMP 诱捕代码骨架已验证可用（保护诱饵区可抓 CPU 写者 PC，DMA 写者不触发）。**全部测试/验证/调试工具的说明见 `tools/README.md`**。
- **Windows usbser 主机侧行为**：单笔大写 pending 期间饿死读（测试须分块写+低波特率加 10ms 节奏）；数据完整性与设备无关。
- **[致命·环境] Windows usbser 运行时状态疲劳（2026-10-03 实锚，重启治愈）**：调试期高频重枚举+序列号轮换（19→33→34→19，当日 50+ 次）+枚举窗口期 halt 的综合欠账后，Windows **停止对 CDC 接口下发 EP0 类请求**（SET_LINE_CODING/SET_CONTROL_LINE_STATE 全不发）——固件侧表现：`uart_started=0`、`g_led_open.com=0`、回环 0 字节、OUT 池灌满后写超时；**同设备 WinUSB 接口（DAP/CAN）与 bulk OUT 全正常**（3079B 逐字到达），标准请求枚举正常。判别流程已走全：纯 HEAD 已知好固件同样复现（排除代码改动）、`pnputil /disconnected` 无活幽灵、UsbFlags/Enum 注册表缓存全惰性（排除持久缓存）→ **重启 Windows 后 11/11 全 PASS + 5M 满载锤 5/5 PASS**。教训：(1) usbser 运行时态只有重启能清空，重插/换口无效；(2) 序列号轮换=每次全新 devnode+COM 号漂移+永久注册表疤（`Enum\USB\VID_0D28&PID_0218` 下已积累 6 个序列号键+1 个无序列号幽灵键 `7&11345d59&2&4`，幽灵源自枚举窗口期 halt 致串读取失败回退端口实例 ID）；(3) `UsbFlags\<VID+PID+REV>` 缓存 MS OS 1.0 探测结果（osvc，不含序列号），MS OS 2.0 走 BOS 每次枚举重读不受此门控；(4) 排查此类问题的差分利器：WinUSB vs usbser 同设备双通路 + g_dbg/uart_started/g_led_open 计数器对账（openocd 单次 halt/mdw），不必先怀疑固件。
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
- CAN（2026-10-01 真实总线打通）：**引脚：CAN1=PA12(TX)/PA11(RX)（默认映射，v1 板布线实锚；曾误用 Remap1=PB8/9，静默回环不驱动总线故从未暴露）、CAN2=PB13(TX)/PB12(RX)（固定）**。bxCAN 无 DMA 请求线——FMP 中断收帧 + TX 空中断续装。APB1=72MHz，1M 波特率 = P4 × 18tq。**BTIMR 位段按厂商库：BRP[9:0] SJW[25:24] TS2[22:20] TS1[19:16]**（曾把 TS2 写在 [15:12] 无效位）。**WCH bxCAN 真总线帧实测进 FIFO1 而非 FIFO0**（FAFIFOR 与 STM32 语义不符且高位只读）——固件双 FIFO 收割 + FMP0/FMP1 双中断。`CAN_FilterInit` 只收一个参数（bank 序号决定归属，`CAN_SlaveStartBank` 切分 28 组 bank）。WCH 库 `CAN_Init` 成功返回 1。**静默回环自测覆盖不了引脚映射与 FIFO 路由——真总线测试 `tools/can_bus_test.py`（正常模式双通道对接）是 CAN 回归必跑项**。收发器 SN65HVD251，总线 60Ω 终端。
- **[致命] CAN 换模式禁止裸写 BTIMR，必须整跑库 `CAN_Init`（2026-10-02 真机实锤）**：本封装（chippackid 4-7）的 `CAN_Init` 内嵌出厂自检序列（RCC 复位脉冲 → 4Mbps 静默回环自测帧 → 再复位脉冲），每次调用洗净全部影子状态；官方 EVT TestMode 例程换模式也从不裸写 BTIMR、每次重跑 `CAN_Init`。自研"INRQ→写 BTIMR→退 init"轻量路径在控制器**有过睡眠史后**不可靠：退 init 时测试模式采样出错，NORMAL 带**隐藏回环**（可发送/收自己/拒收总线，与 RM 24.3.2 回环定义逐字吻合），而 BTIMR 读数完全正常（寄存器面干净、核心行为错位）——表现为真总线双通道交叉 reception 全灭、`can_bus_test.py` 收 0，且回环自测仍 PASS（掩盖故障）。修复：`can_hw_reconfig` 全程走 `CAN_Init` + `CAN_Sleep`（RM Fig24-1 唯一合法睡眠边 Normal→Sleep）+ **每次 CAN_Init 后必须重挂过滤器**（自检段会重写 bank0/1 并 `CAN_SlaveStartBank(1)`）。RM 精读要点：CTLR bit16=DBF 复位值 1（正常态非隐藏位）；bit15=控制器内置软复位；睡眠态仅邮箱寄存器可访问。上电默认真 close（控制器睡眠，SET_MODE 显式打开）已落地，RM 24.2.1 亦记载复位后默认睡眠。
- **[致命] `bsp_uart_write` 的 TC 轮询在 USART 未使能时死锁**：CH32V30x 上 TE 从未使能则 STATR.TC 恒 0（与 STM32 复位值 1 的经验相反），`rt_hw_console_output`→`bsp_uart_write` 的 INIT 阶段打印会把整个启动链卡死（现象：全灯灭+USB 不枚举+所有线程 TCB 全零，openocd 读 PC 停在 TC 轮询）。UART 上电默认 close 后，`bsp_uart_write` 必须 `!uart_started` 直接丢弃返回。
- **openocd halt 会打断 Windows 枚举窗口且软件侧无法恢复**：flash 后枚举进行时 halt（或 halt/resume 循环）→ Windows 枚举失败后不再重试，`reset run`/`pnputil /scan-devices` 均救不回，只能物理重插。观测 RAM 用 `init/halt/mdw/resume/shutdown` 一次性完成，别在 flash 后立刻 halt。
- **[致命] WCH SysTick 的 SR（COUNTFO）必须软件清除**：`SysTick_Handler` 里不清 `SysTick->SR=0` 则 mret 后立刻重触发=中断风暴，主循环饿死（USB 不枚举、毫秒计数以 MHz 级狂奔）。APP 侧 board.c 的 handler 一直有这行；boot 自建 SysTick 时漏了，2026-10-03 实锚。连带坑：**裸机 MIE=0 上下文（如 boot 的 jump_to_app 全程）不能用中断式 delay**——SysTick 不达、计数冻结、死等，须忙等变体（boot_delay_ms_blocking）。
- **[致命] openocd `flash erase_address` 在 WCH SDI 驱动上是整片擦（2026-10-03 实锚）**：请求擦 0x3F000 旗标页 4KB，实际全片 flash 变 e339e339 擦除态（boot+APP 全灭，设备从总线消失）。**永远不要用它做局部擦**；旗标页的擦除走 boot 协议 `BPC_ERASE_FLAG` 或顺其自然保持擦除态。同日第二坑连坐：**openocd flash 操作（erase/program）会把 FLASH_CTLR 留在脏态（解锁未锁回），此后 `resume` 让固件一跑（如 SystemInit 写 CTLR 位）即触发整片擦除**——烧录后正确姿势：`program ... verify` 后直接 `shutdown`（目标停在 halt），由人工 POR 干净启动；观测型 halt/mdw/resume 无 flash 操作则安全（本仓当日多次验证）。
- **boot HID 的 LANGID 坑（2026-10-03 实锚）**：CherryUSB 核心层对 `GET_DESCRIPTOR(String, index=0)` 要求回调**返回非 NULL**（取返回前 2 字节合成 4B LANGID 描述符），回调对 index 0 返回 NULL = STALL → Windows 枚举止步于 string0、**不发 SET_CONFIGURATION** → HID 设备 Code 10（FAILED_START），且 FAILED_START 后 Windows 连重试控制传输都不发（scan-devices 无效）。boot/usb_glue.c 已修为 APP 同款（返回 `string_descriptors[0]`）。判别利器：boot 自带 SETUP 环形日志（0x2000C800，8 组×8B+[64]=wr）+ pump 存活计数（[65]，双采样判主循环生死）。

## 开发铁律

- **[铁律·自杀性测试禁令（2026-10-01 实锚案例）]** 任何可能切断芯片自身恢复途径的操作（SWJ_Disable 关调试口、关 USB 而它是唯一烧录通路、PFIC 调试位、看门狗不可喂等），**禁止未经“已验证的恢复路径”就上真机测试**——且必须先自问“这代码若每次开机都跑，我还能不能烧录”。本板无 BOOT 测试点，SDI 一断只能物理抢救。具体地：**CH32V305 上 `GPIO_Remap_SWJ_Disable` 会在运行期切断两线 SDI 调试口（wch-link 立即失联），且 PB3/PB4 默认即可作 GPIO/SPI 复用，永远不需要它**——app/Bsp/device/dap/bsp_dap.c 已彻底移除并留注释。
- **[铁律] 遇到顽固 bug 必须调用 `engineering-method` skill（用户级 `~/.zcode/skills/engineering-method/`），不得凭惯性继续排查**。触发线（任一）：同一故障第 2 个假说死亡；开始怀疑 OS 补丁/硅级 bug/竞态/"玄学"等外因；"已排除"的结论被反复拿出来重审；修了真实缺陷但原始症状纹丝不动。触发后停止生产新假说，按 skill 的四条增量纪律执行：排除结论必须带覆盖清单、外因假说须机制或对照实验、修复≠根因（正交缺陷陷阱）、真值三源对照（规范表格/vendored 常量/真机抓包）。MS OS 2.0 免驱排查（根因 2 字节、疑点十几个、耗时数周）是其锚点案例，翻车详情见 skill 的 references。

## 约定

- 注释、commit message、文档用中文；commit 风格见 `git log`（短中文描述）。
- 命名：厂商库 `ch32v30x_` 前缀，BSP 层 `bsp_` 前缀（`app/Bsp`），RT-Thread 用 `rt_` 前缀，应用层小写下划线。
- 调试输出用 `rt_kprintf()`；USB 协议栈日志走 `CONFIG_USB_PRINTF()` 宏。
- 状态 LED 共 5 个（STA/SWD(DAP)/COM/CAN1/CAN2），统一通过 `bsp_led_ctrl/toggle` 操作，高电平为灭。**灯语（2026-10-02 定稿，monitor 线程唯一渲染，数据路径零 LED 代码）**：STA=USB 未连接 1s 周期闪烁/连接后常亮（**boot 升级态=500ms 周期快闪**，SysTick 基准，boot/User/bsp_led.c，与 APP 的 1s 闪状态可辨）；四个功能灯=未 open 灭、open 无数据常亮、有数据 200ms 固定间隔闪烁（活动窗口增量优先于 open 标志）。**open 信号源**：COM=主机 DTR（强符号覆盖 CherryUSB `__WEAK usbd_cdc_acm_set_dtr` 写 `g_led_open.com`，USB RESET 清零）；DAP=官方 `DAP_HostStatus` 命令（DAP.c→`LED_CONNECTED_OUT` 宏改写 `g_led_open.dap_conn`，OpenOCD 会话进出发 connected=1/0，CONFIGURED 清零兜底）；CAN1/2=硬件通道模式（`bsp_can_get_mode != SLEEP`，SET_MODE 驱动）。活动计数源：g_dbg（COM=uart_submits+tx_dma，CAN=canN_tx+rx）、`g_dap_dbg[1]`（DAP OUT 回调）。
- 新线程的优先级/栈大小必须加到 `app/User/config.h`，不要在源文件里写魔数。

## 当前进度（2026-10）

- CDC↔UART 双向块缓冲零拷贝：已落地，921600–9M 全档位零丢失（回归 11/11 PASS）。**UART/USART 上电默认 close（延迟启动）**：首次线控配置才启动 USART+RX DMA，COM 灯 open=DTR；DTR=0 只灭灯不 deinit（SET_LINE_CODING 有 memcmp 去重，deinit 后重开口不重发线控会断数据路径）。
- CAN 双路驱动 + CAN-USB 桥（WinUSB 复用双路）：已落地，**真实总线双向千帧零丢失（can_bus_test.py）+ 静默自测双 PASS**。**上电默认真 close（控制器睡眠）+ SET_MODE 显式 open**（2026-10-02 落地，换模式硅级坑见上）。
- **LED 灯语系统（2026-10-02 落地）**：monitor 线程统一渲染 5 灯（STA 1s 闪/常亮；COM/DAP/CAN1/2 灭/常亮/200ms 闪），open 信号锚定硬件/协议语义（DTR/HostStatus/CAN 模式），历经回环+真总线+混压回归全绿。
- MS OS 2.0 免驱：根因（描述符类型字段违反规范表 9）已修，真机免驱绑定 WinUSB 验证 PASS。
- **DAP bulk v2 通路已打通（2026-10-01）**：接口 2（"CMSIS-DAP"），OpenOCD 不接目标板可读完整 link 信息（v2/SWD/Atomic/FW 2.1.1/引脚状态），与 CAN 回归双绿。
- DAP/SWD：**全链路已打通（2026-10-01 真机验证）**：SWD 引脚引擎 `app/Bsp/device/dap/bsp_dap.{c,h}`（SWCLK=PB3/SWDIO=PB5，GPIO 位拍为**现役引擎**，有效 SWCLK ~5.3MHz）；对 STM32F103C8T6 真机完成 DP/AP 全枚举（DPIDR=0x1BA01477，Cortex-M3 r1p1，6 断点 4 观察点）。**全功能测试 PASS（同日）**：halt/resume/step、内核寄存器、RAM 读写回环、硬件断点（FPB 精确命中 Reset_Handler）、数据观察点（DWT 捕获 .data 写）、flash 读/全擦/编程/128KB 逐字节校验全循环、SWD 时钟 2/4/8MHz 全稳定（实测验证吞吐 59KiB/s@4MHz；DAP_THREAD_STACK 因此 1024→1536，压测水位 672B）。**SPI 加速暂存**：v1 板缺 MISO 桥接（全双工+串联电阻路线需 PB4 直连 SWDIO、PB5 经 ~1K 电阻），完整调查与 v2 恢复步骤见 `docs/spi-accel-swd-worklog.md`；引擎骨架与 `BSP_DAP_USE_SPI` 宏已在库。WCH 版 openocd 的 target/*.cfg 有损坏的脚本搜索前缀，勿用 `-f target/stm32f1x.cfg`，用手工 DAP 声明（命令已固化 tools/README.md）。
- **USB bootloader 已落地并全流程验证（2026-10-03，`docs/bootloader-design.md`）**：分区 boot@0x0(≤20KB 裸机)/APP@0x5000(.appsig 魔法字)/旗标页@0x3F000；触发链 = APP 侧 dap_thread 拦截 `ID_DAP_Vendor0` → 写 BOINIT RAM 旗标（0x2000D000，软复位存活）→ `NVIC_SystemReset`；boot 态 = 单 HID（PID 0x0219 "UniConnect Bootloader"）走 GET_INFO/BEGIN/PROGRAM/FLUSH/VERIFY/END/REBOOT 协议。**端到端实锚全绿**：APP 触发→boot 传输 78KB/6.4s→CRC32→REBOOT 回 APP（连续两轮），升级后 CDC 回环 11/11 + DAP link + 双路 CAN 2000 帧全 PASS。APP 侧三件套在 `app/app.ld`（平移 0x5000/232K + RAM 52K + BOINIT 区）、`app/User/main.c`（.appsig）、`app/Application/dap_thread.c`（Vendor0 拦截）；`APP_SIG_MAGIC` 单一事实源在 `common/boot_shared.h`。开发期注意：boot/APP 双 program 后需人工 POR；调试进 boot 可用 openocd `mww 0x2000D000 0x5AA55AA5` + `reset run`（无 flash 操作安全）配合 fw_update `--skip-trigger`，全程无需物理操作。boot 自带诊断：SETUP 环形日志 @0x2000C800 + pump 计数 @[65]。**封板级加固（2026-10-03）**：无 GOOD 记录一律进升级模式不跳转（中断的首轮升级会留下"签名在而镜像半写"现场，签名不作放行凭证，GOOD 记录 len+crc 是唯一凭证）；注入测试实锚全绿——传输 52% 处杀进程+复位→仍留 boot 态、ERASE_FLAG+复位→仍留 boot 态、恢复升级→正常跳 APP。**出厂流程固化：`flash_all` → 跑一次 fw_update（写入首份 GOOD）→ POR 验证跳 APP → 才可密封**（openocd 直烧后设备停在 boot 态属预期）。封板口径=Release 构建（boot -Os 82%/APP 37.6KB），Release 全回归+栈水位已绿（各线程 8–28%）。**封板风险决策（2026-10-03 拍板接受，勿当遗漏重提）**：(1) 无强制进 boot 手段——属发版质量问题，发版流程保证不出"上电即无 USB"的不可运行 APP，不加自救/监听窗口；(2) flash 位翻转老化——APP/旗标区有 CRC 自愈，boot 单副本裸奔属器件寿命范畴接受（唯一实际杠杆=灌封热设计，控制结温）。
- **CMake 化 + 目录四分法重组（2026-10-03 落地）**：`app/`（APP 固件）/`boot/`（boot 固件）/`common/`（Core+Peripheral+Startup+boot_shared.h+system 头）/`third_party/`（CherryUSB+RtthreadNano **vendor 化收编**（去子模块固定 v3.1.5）+Patch）；构建=CMake+Ninja（模块=INTERFACE 源集，flash_app/flash_boot/flash_all 烧录目标）。**等价性已验**：APP 418/418、boot 191/191 符号大小与 make 基线全同（text 差为链接顺序 padding），编译 flag 逐项核对一致；boot Debug 特例 -O1（-O0 溢 20KB 分区，19240B/94%）。RtthreadNano 子模块身份已移除，`.gitmodules` 删除。
- 下一步：SWD 深化（halt/读写内存/闪存算法）、DAP 加速引擎（SPI 后端）、CAN 接口改第二 CDC 量产路线评估、boot 冻结定型（改动前评估必要性）。
