/* fw_upgrade.c -- 固件升级库实现(第1期骨架: 生命周期与探测为真, 协议为桩)
 * 协议常量经 boot.h 直引固件(单一事实源)。 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "uniconnect_upgrade.h"
#include "boot.h"          /* 固件 boot/User/boot.h: HID升级协议命令字(host-safe) */
#include "uni_transport.h"

struct fwu_ctx {
    int dummy;             /* 第2期: 传输句柄/协议会话状态 */
};

uint32_t fwu_abi_version(void)
{
    return UNI_ABI_VERSION;
}

const char *fwu_strerror(int err)
{
    switch (err) {
    case UNI_OK:           return "ok";
    case UNI_ERR_PARAM:    return "invalid parameter";
    case UNI_ERR_NOMEM:    return "out of memory";
    case UNI_ERR_NOT_FOUND:return "device not found";
    case UNI_ERR_BUSY:     return "device busy";
    case UNI_ERR_IO:       return "usb io error";
    case UNI_ERR_TIMEOUT:  return "timeout";
    case UNI_ERR_NOT_SUP:  return "not supported (this build/platform)";
    case UNI_ERR_PROTO:    return "device protocol error";
    case UNI_ERR_STATE:    return "call sequence error";
    case UNI_ERR_CRC:      return "crc mismatch";
    default:               return "unknown error";
    }
}

int fwu_open(fwu_ctx **ctx)
{
    if (!ctx) return UNI_ERR_PARAM;
    *ctx = calloc(1, sizeof(fwu_ctx));
    return *ctx ? UNI_OK : UNI_ERR_NOMEM;
}

void fwu_close(fwu_ctx *ctx)
{
    free(ctx);
}

int fwu_device_state(void)
{
    switch (uni_transport_scan()) {
    case UNI_DEV_APP:  return 0;
    case UNI_DEV_BOOT: return 1;
    default:           return UNI_ERR_NOT_FOUND;
    }
}

int fwu_trigger_bootloader(fwu_ctx *ctx, unsigned timeout_ms)
{
    (void)ctx; (void)timeout_ms;
    return UNI_ERR_NOT_SUP;   /* 第2期: open_dap + Vendor0[UNI_DAP_CMD_FW_TRIGGER] */
}

int fwu_wait_bootloader(fwu_ctx *ctx, unsigned timeout_ms)
{
    (void)ctx; (void)timeout_ms;
    return UNI_ERR_NOT_SUP;   /* 第2期: 轮询 scan()==BOOT */
}

int fwu_flash(fwu_ctx *ctx, const uint8_t *image, uint32_t len,
              fwu_progress_cb cb, void *user)
{
    (void)ctx; (void)image; (void)cb; (void)user;
    if (!image || !len || len > (uint32_t)APP_MAX_SIZE) return UNI_ERR_PARAM;
    return UNI_ERR_NOT_SUP;   /* 第2期: BEGIN/PROGRAM/FLUSH/VERIFY/END */
}

int fwu_reboot(fwu_ctx *ctx)
{
    (void)ctx;
    return UNI_ERR_NOT_SUP;   /* 第2期: BPC_REBOOT */
}

int fwu_upgrade_file(const char *path, fwu_progress_cb cb, void *user,
                     char *err_detail, size_t err_detail_sz)
{
    (void)path; (void)cb; (void)user;
    if (err_detail && err_detail_sz) {
        snprintf(err_detail, err_detail_sz, "not implemented in this build");
    }
    return UNI_ERR_NOT_SUP;   /* 第2期: 读文件+触发+传输+REBOOT 全链 */
}
