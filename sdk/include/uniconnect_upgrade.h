/* fw_upgrade.h -- UniConnect 固件升级库 (对 APP(0x0218) 触发 + 对 boot(0x0219) 传输)
 *
 * 两阶段模型(与 tools/fw_update.py 等价):
 *   阶段1(APP态): WinUSB DAP接口发 Vendor0 -> 设备写BOINIT旗标+复位
 *   阶段2(boot态): HID(PID 0x0219)走 GET_INFO/BEGIN/PROGRAM/FLUSH/VERIFY/END/REBOOT
 * 高层入口 fwu_upgrade_file() 一把梭; 低层函数供进度回调/中断恢复场景。
 *
 * 协议常量直引固件 boot/User/boot.h(单一事实源, host-safe契约)。
 * 用法示例与线程/错误规约见 sdk/README.md。
 */
#ifndef UNICONNECT_UPGRADE_H
#define UNICONNECT_UPGRADE_H

#include "uniconnect_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fwu_ctx fwu_ctx;   /* 不透明上下文 */

/* 升级进度回调: done=已传输字节, total=镜像总长, user=调用者透传;
 * 返回非0则中止升级(干净收尾, 设备留在boot态可重试) */
typedef int (*fwu_progress_cb)(uint32_t done, uint32_t total, void *user);

UNI_API uint32_t fwu_abi_version(void);
UNI_API const char *fwu_strerror(int err);

/* --- 生命周期 --- */
UNI_API int fwu_open(fwu_ctx **ctx);                    /* 分配上下文(不触设备) */
UNI_API void fwu_close(fwu_ctx *ctx);

/* --- 阶段1: APP态触发(设备须以 0x0218 枚举且DAP接口可用) --- */
UNI_API int fwu_trigger_bootloader(fwu_ctx *ctx, unsigned timeout_ms);

/* --- 阶段2: boot态传输(等待 0x0219 HID 出现后走完整协议) --- */
UNI_API int fwu_wait_bootloader(fwu_ctx *ctx, unsigned timeout_ms);
UNI_API int fwu_flash(fwu_ctx *ctx, const uint8_t *image, uint32_t len,
                      fwu_progress_cb cb, void *user);  /* BEGIN..VERIFY..END..REBOOT */
UNI_API int fwu_reboot(fwu_ctx *ctx);                   /* 仅跳APP(镜像已END) */

/* --- 高层一把梭(串起以上全部, err_detail回写文本可选) --- */
UNI_API int fwu_upgrade_file(const char *path, fwu_progress_cb cb, void *user,
                             char *err_detail, size_t err_detail_sz);

/* 设备在线探测: 返回 0=APP态, 1=boot态, 负值=错误(不在线/IO) */
UNI_API int fwu_device_state(void);

#ifdef __cplusplus
}
#endif

#endif /* UNICONNECT_UPGRADE_H */
