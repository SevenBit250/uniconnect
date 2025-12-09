# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

UniConnect 是一个基于 CH32V305CCT6 微控制器的 CMSIS-DAP 调试探针项目，支持 SWD 调试、CDC 串口和双路 CAN 功能。该项目使用 RISC-V 架构的微控制器，运行 RT-Thread Nano 操作系统。

## 构建系统

### 开发环境
- IDE: Eclipse CDT
- 工具链: RISC-V Cross GCC
- 构建系统: Eclipse Managed Build（自动生成 Makefile）

### 构建命令
```bash
# 在 Eclipse IDE 中：
# 1. 右键项目 -> Build Project
# 或使用快捷键 Ctrl+B

# 命令行构建（如果需要）：
# 进入 obj 目录，执行 make
cd obj
make
```

### 项目配置
- 使用 `.project` 和 `.cproject` 文件存储 Eclipse 项目配置
- 编译输出目录：`obj/`
- 优化级别：`-Os`（优化代码大小）
- 目标架构：RV32I + RVM + RVA + RVC（RISC-V 32位整数指令集 + 乘法 + 原子操作 + 压缩指令）

## 高层架构

### 硬件抽象层
- **Core**: RISC-V 内核支持 (`core_riscv.c`)
- **Peripheral**: CH32V30x 外设驱动库，支持 CAN、USART、USB、GPIO、DMA、TIM、SPI、I2C 等
- **Startup**: 启动代码和中断向量表
- **Ld**: 链接脚本，定义内存布局

### 系统层
- **RT-Thread Nano**: 轻量级实时操作系统，提供线程管理和基础服务
- **User**: 用户代码层，包含：
  - `main.c`: 主程序入口，初始化系统和 CDC-ACM 功能
  - `DAP_config.h`: CMSIS-DAP 调试接口配置
  - `ch32v30x_conf.h`: 芯片功能配置
  - 中断处理程序 (`ch32v30x_it.c`)

### 应用层
- **App**: 应用程序入口，管理 SWD 调试线程
- **UsbClass**: USB 设备类配置，主要实现 CDC-ACM（虚拟串口）
- **Usb2Device**: USB 转设备功能层，包含：
  - `usb2uart.c`: USB 转 UART 实现
  - `usb2can.c`: USB 转 CAN 接口（待实现）

### 协议栈层
- **CherryUSB** (v1.51): 完整的 USB 协议栈
  - 支持 CDC、HID、MSC、DFU 等设备类
  - 提供 CH32 系列微控制器的移植层 (`port/ch32/`)
  - 集成 RT-Thread 操作系统抽象层 (OSAL)
- **CherryRB**: 环形缓冲区库，用于数据传输缓冲

### 关键功能流程

1. **系统启动流程**：
   ```
   startup_xxx.S -> SystemInit() -> main() ->
   rtthread_startup() -> app_entry() -> swd_thread_startup()
   ```

2. **USB 初始化流程**：
   ```
   usb_dc_low_level_init() -> cdc_acm_init() ->
   CherryUSB stack -> USB 枚举
   ```

3. **状态指示**：
   - 使用 5 个 GPIO 引脚作为状态指示灯：
     - `SWD_STA_Pin`: SWD 调试状态
     - `SYS_STA_Pin`: 系统状态
     - `COM_STA_Pin`: 串口通信状态
     - `CAN1_STA_Pin`: CAN1 通道状态
     - `CAN2_STA_Pin`: CAN2 通道状态

## 开发注意事项

### 时钟配置
- 系统主频：144MHz
- USB 时钟：使用 USBPHY 48MHz 时钟源
- USB PLL：使用 HSE 作为参考时钟

### 内存管理
- 使用 RT-Thread 的动态内存管理
- USB 数据缓冲区使用 `__attribute__((section(".noncacheable")))` 避免缓存问题

### 调试输出
- 使用 `rt_kprintf()` 进行调试信息输出
- USB 协议栈调试通过 `CONFIG_USB_PRINTF()` 宏输出

### GPIO 配置
- 状态指示灯初始状态：高电平（灯灭）
- GPIO 配置：推挽输出，50MHz 速度

## 当前开发状态

根据 README.md，项目各功能模块状态：
- [x] 基础框架
- [x] USB 通信（CDC-ACM 基本实现）
- [ ] SWD 调试（框架已搭建，线程空实现）
- [ ] CDC 串口（基础功能已实现）
- [ ] 双路 CAN（待开发）

## 文件命名约定

- CH32V30x 系列驱动文件使用 `ch32v30x_` 前缀
- USB 相关文件使用 `usb_` 或 `usbd_` 前缀
- RT-Thread 相关使用 `rt_` 前缀
- 应用层文件使用小写下划线命名