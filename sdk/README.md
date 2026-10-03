# UniConnect SDK

UniConnect 探针的宿主动态库：**uniconnect_upgrade**（固件升级）与 **uniconnect_can**（CAN 通讯），C ABI，跨语言可调（C/C++/C#/Python/LabVIEW…）。

> **实现状态（2026-10-03 第 1 期）**：构建/ABI/交付面已定型并全部通过冒烟测试；`fwu_device_state`、两库生命周期与错误码为**真实现**，USB 传输协议函数（触发/传输/收发）返回 `UNI_ERR_NOT_SUP`，第 2/3 期接入 libusb/hidapi 后逐函数点亮（接口不变）。

## 获取与构建

SDK 随固件仓库一起构建（monorepo），产物落 `build/sdk/`：

```bash
cmake -B build -G Ninja          # 一次配置（固件+SDK 同一命令）
cmake --build build              # 全量构建；也可 --target sdk 只出 SDK
```

- 宿主工具链：GCC（MinGW-w64），默认取 `C:\Portable\mingw64\bin\gcc.exe`，可用环境变量 `GCC_BIN` 覆盖；**不使用 MSVC 编译**（全开源阵营）
- 单独构建：`cmake -S sdk -B build-sdk -G Ninja -DCMAKE_C_COMPILER=<gcc> -DSDK_DIST_DIR=<输出目录>`

## 交付物清单（build/sdk/）

```
uniconnect_upgrade.dll / .lib / .dll.a / .def    固件升级库
uniconnect_can.dll      / .lib / .dll.a / .def    CAN 通讯库
include/uniconnect_upgrade.h / uniconnect_can.h / uniconnect_common.h   # 三个自包含头
```

- `.lib`：dlltool 生成的 COFF 导入库，**MSVC 链接器可直接用**
- `.dll.a`：GNU 工具链（gcc/clang）用导入库
- `.def`：显式导出清单（ABI 契约，也是自行再生成导入库的原料）
- Linux 构建对应产出 `uniconnect_*.so`（第 2 期开放）
- 三个头文件**零外部依赖**（协议常量在打包时从固件头内联，`uniconnect_common.h`/`uniconnect_can.h` 自包含），拷走即用

## ABI 规约（两库一致）

| 约定 | 内容 |
|---|---|
| 链接 | 纯 C ABI（`extern "C"`），C++ 直接 include |
| 句柄 | 不透明指针（`fwu_ctx*`/`can_ctx*`），内部布局不承诺 |
| 错误 | `int` 错误码（`UNI_OK`=0，负值见 `uniconnect_common.h`），`*_strerror` 取文本 |
| 内存 | 缓冲区一律调用者提供（容量入参/实长出参）；库不对调用者 malloc |
| 字符串 | UTF-8 |
| 线程 | 句柄**非线程安全**：一通道一线程；跨线程由调用者加锁 |
| 版本 | `*_abi_version()` 与头内 `UNI_ABI_VERSION` 对账后再用 |
| 演化 | 结构体布局冻结只增不改；新功能只加函数 |

协议常量（CAN 模式值、升级命令字、VID/PID/接口/端点）与固件**单一事实源**（`common/can_proto.h`、`common/usb_ids.h`、`boot/User/boot.h`），打包时内联进交付头，不存在两份漂移。

## 使用示例

### 固件升级（C）

```c
#include "uniconnect_upgrade.h"

int main(void)
{
    char err[256] = {0};
    int rc = fwu_upgrade_file("app.bin", NULL, NULL, err, sizeof(err));
    if (rc != UNI_OK) {
        printf("升级失败: %s (%d)\n", err, rc);
        return 1;
    }
    return 0;   /* 完成: 设备已重启回 APP 态 */
}
```

进阶：`fwu_trigger_bootloader` / `fwu_wait_bootloader` / `fwu_flash`（带进度回调）/ `fwu_reboot` 手动分步，可自定义进度 UI 与中断恢复。

### CAN 通讯（C）

```c
#include "uniconnect_can.h"

int main(void)
{
    can_ctx *ctx = NULL;
    can_frame tx = { .id = 0x123, .dlc = 8, .data = {1,2,3,4,5,6,7,8} };
    can_frame rx;

    if (can_open(&ctx) != UNI_OK) return 1;
    can_set_bitrate(ctx, 0, 1000);          /* CH0 = 1 Mbps */
    can_set_mode(ctx, 0, CAN_MODE_NORMAL);  /* 上总线 */

    can_send(ctx, 0, &tx, 1000);
    if (can_recv(ctx, 1, &rx, 1000) == UNI_OK) {  /* CH1 收(需对接总线) */
        /* rx.id / rx.dlc / rx.data */
    }

    can_set_mode(ctx, 0, CAN_MODE_SLEEP);   /* 收工回睡眠(省电/静默) */
    can_close(ctx);
    return 0;
}
```

### 其他语言

- **C#**：P/Invoke 声明 `[DllImport("uniconnect_can")]`，链 `uniconnect_can.lib` 或运行时 `LoadLibrary`
- **Python**：`ctypes.CDLL("uniconnect_can.dll")` 按头文件签名映射；结构与 `can_frame` 布局一致（`struct.pack`/`Structure`）

## 图形界面示例（example/）

两个开箱即用的 tkinter GUI（Python 3 标准库，仅 ctypes 依赖，无需 pip 安装）：

```bash
python sdk/example/upgrade_gui.py    # 固件升级：选文件、显示镜像信息(大小/CRC32/预览)、进度条、日志
python sdk/example/can_gui.py        # CAN 工具：双通道面板，覆盖 can_comm.h 全部操作
```

- DLL 自动发现顺序：环境变量 `UNICONNECT_SDK_DIR` → `build/sdk/`（monorepo 构建产物）→ 脚本目录
- 升级示例：`fwu_device_state` 设备探测、`fwu_upgrade_file` 后台线程升级，进度回调经队列投递到 UI 线程（tk 的 after 轮询），窗口不卡死
- CAN 示例：每通道独立面板（`set_bitrate` / `set_mode`（正常/静默回环/睡眠）/ `send` / `get_status` / `flush_rx`），接收用 `can_recv` 轮询线程持续收取，两通道操作互不干扰——也是"句柄非线程安全，一通道一线程"ABI 约定的示范用法

## 链接方式速查

| 你的工具链 | 链接物 |
|---|---|
| MSVC (cl) | `uniconnect_*.lib` + 运行时 dll 在 PATH |
| GCC/Clang (MinGW) | `-luniconnect_can`（给 `.dll.a`）+ dll 在 PATH |
| 动态加载 | `LoadLibrary`/`dlopen` 直接上 dll；导出名见 `.def` |

## 测试

```bash
cmake --build build/sdk-host --target test    # CTest: ABI往返 + 交付头自包含
```

## 目录

```
include/   三个交付头(源头; 打包时经 amalgamate.py 聚合+内联固件协议头)
src/       实现 + uni_transport(平台收敛层) + .def 导出契约
tests/     ABI 冒烟 + 交付头自包含测试
example/   Python tkinter GUI 示例(升级 + CAN 工具)
amalgamate.py   交付头聚合器(5头→3头)
host.cmake 挂进固件根构建的嵌套构建定义
```
