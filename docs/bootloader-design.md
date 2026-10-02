# UniConnect Bootloader 设计（2026-10-02 探索定稿）

依据：CH32F/V2x/V3x RM V2.2 第 1/32 章（flash 组织、boot 模式）、EVT `EXAM/IAP/USB_UART`
官方双工程参考、本仓真机实锚（SRAM_CODE_MODE=10x: CODE 256KB + RAM 64KB，openocd 报
288K 为 die 容量）。板无 BOOT 测试点 → 出厂 ROM ISP(0x1FFF8000) 不可用，纯软件 bootloader。

## 分区图（CODE 256KB @0x00000000）

```
0x0000_0000 ┌─────────────────────────┐
            │ Bootloader (≤20KB)      │  裸机, 无RTOS, CherryUSB(osal_none)+HID单接口
0x0000_4FFF │ (预留对齐到 0x5000)      │  定型后冻结, 独立工程 boot/
0x0000_5000 ├─────────────────────────┤
            │ APP (228KB)             │  现工程整体平移, Link.ld ORIGIN=0x5000
0x0003_EFFF │                         │
0x0003_F000 ├─────────────────────────┤
            │ 升级旗标页 (4KB, 对齐页) │  0x3FF00 处写 0x5AA55AA5 = 进升级模式
0x0003_FFFF └─────────────────────────┘
```

- 旗标放独立 4KB 区（0x3F000 起）：APP 擦写自己的代码区不会误碰旗标页。
- 256B 快编程单位（RM 32.2.1），bootloader 的快编程函数位于前 20KB 零等待区（RM 要求）。

## 触发链路

```
主机工具 fw_update.py
  ├─ 阶段1(APP态): WinUSB接口2 发 DAP厂商命令 ID_DAP_Vendor0(0x80)
  │    └→ dap_thread 拦截(不进DAP_ExecuteCommand):
  │         写 0x5AA55AA5 @0x3FF00 → 回 DAP_OK → NVIC_SystemReset()
  └─ 阶段2(boot态): hidapi 枚举 VID=0x0D28 PID=0x0219 名称"UniConnect Bootloader"
       └→ 升级协议传镜像 → END → 跳 APP
```

- APP→boot 唯一入口 = 旗标+复位（官方 IAP 同款机制，断电安全，升级失败永远回得去 boot）。
- boot 启动：无旗标且 APP 校验过 → 跳 0x5000；否则停在升级模式。

## USB 形态

| 形态 | 接口 | VID | PID | 识别 |
|---|---|---|---|---|
| APP(现行) | CDC×2 + DAP bulk + CAN(复合) | 0x0D28 | 0x0218 | 不变 |
| Bootloader | 单 HID(升级报告) | 0x0D28 | **0x0219** | 产品串"UniConnect Bootloader" |

独立 PID：Windows 设备缓存全新节点（MSOS 排查同款经验）；工具无歧义识别当前模式。

## 升级协议（HID 报文，主机→设备 64B / 设备→主机 64B）

```
请求: [cmd:1][seq:1][len:2][addr:4][rsv:2][payload:52]  (共60B有效+对齐)
应答: [cmd:1|0x80][seq:1][status:1][crc_ok:1][rsv:4][echo:52]

cmd:
  0x01 GET_INFO   → 应答: 协议版本/flash大小/APP基址/最大镜像 (payload区)
  0x02 ERASE      addr=APP起始, len=镜像长度 → 按256B页逐页擦(boot分批应答进度)
  0x03 PROGRAM    addr=目标页地址, payload=数据(不足52B补0xFF) → 256B页缓冲凑满即写
  0x04 VERIFY     → boot 读回逐页 CRC32 与主机比对（或主机发期望CRC, boot全片算）
  0x05 END        → 校验通过则清旗标页+回OK, 主机发复位或boot自动跳APP
  0x06 REBOOT     → 直接跳APP(不清旗标外的动作)
```

镜像完整性：ERASE 前主机发镜像长度+CRC32（GET_INFO 后首包或 ERASE 的 payload 携带）；
boot 在 END 时全 APP 区 CRC32 复核，不过则拒绝跳转并报错（防坏镜像变砖）。
APP 自身有效性（无旗标冷启动路径）：首字签名（APP Link.ld 在头部放 4B 魔法字）
+ 尾部 CRC32 复核，任一不过留在升级模式。

## RM 合规要点（实现红线）

1. 擦写前退出增强读模式 ENHANCE_MOD（若系统开启过；复位后硬件自动退出，boot 无此负担，
   **APP 内写旗标需自查**）。
2. FLASH 访问时钟 ≤60MHz（SCKMOD 默认 sysclk/2；144MHz→72MHz 超限）——boot/app 擦写
   前将 SCKMOD 切为 /4 或临时降主频，写完恢复。官方 EVT IAP 未处理，按 RM 走。
3. 快编程函数置于零等待区（boot 区天然满足）。
4. BSY 期间 ADDR 不可写等状态机约束，全部走厂商库 FLASH_*_Fast 封装。

## 工程结构

```
boot/            ← 独立裸机固件工程(CMake 目标 boot.elf, boot/boot.ld 20KB分区)
  User/main.c    状态机: 查旗标→跳APP/HID升级循环
  User/bootproto.c 协议+页缓冲+CRC32
  User/flash_io.c  厂商库快编程薄封装(时钟守卫+ENHANCE_MOD守卫)
  User/usb_glue.c  单HID描述符+裸机泵(CONFIG_USB_BAREMETAL 共用端口驱动)
  复用: CherryUSB(core+hid) + common/ 厂商库最小集(接口源集)
app/             ← APP 固件工程(CMake 目标 uniconnect.elf, app/app.ld ORIGIN=0x5000)
  Application/dap_thread.c Vendor0 拦截 + app/User/main.c .appsig + common/boot_shared.h 触发
common/boot_shared.h  BOINIT/旗标页/APP_SIG_MAGIC 单一事实源(已收敛于此)
tools/fw_update.py  两阶段升级工具
```

构建: `cmake --build build --target boot.elf|uniconnect.elf|flash_all`（2026-10-03 CMake 化）

## 风险与回退

- SDI 烧录口与 bootloader 完全正交——任何时刻 openocd 可恢复（最终回退手段）。
- 变砖窗口分析：boot 自身 ≤20KB 定型后不再变更；APP 损坏最坏情况 = 停在升级模式可重刷。
- 144MHz 擦写时钟红线按 RM 执行（见上），实测异常则以 120MHz 短暂降频兜底。
