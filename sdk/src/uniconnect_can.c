/* can_comm.c -- CAN 通讯库实现(第1期骨架: 生命周期为真, 协议为桩)
 * 协议常量经 can_proto.h 直引固件(单一事实源)。 */
#include <stdlib.h>
#include "uniconnect_can.h"
#include "uni_transport.h"

struct can_ctx {
    int dummy;             /* 第2期: bulk设备句柄+seq计数+接收缓存 */
};

uint32_t can_abi_version(void)
{
    return UNI_ABI_VERSION;
}

const char *can_strerror(int err)
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

int can_open(can_ctx **ctx)
{
    if (!ctx) return UNI_ERR_PARAM;
    *ctx = calloc(1, sizeof(can_ctx));
    return *ctx ? UNI_OK : UNI_ERR_NOMEM;   /* 第2期: 附带 open_can 独占接口 */
}

void can_close(can_ctx *ctx)
{
    free(ctx);
}

int can_set_mode(can_ctx *ctx, uint8_t ch, uint8_t mode)
{
    (void)ctx;
    if (ch > 1) return UNI_ERR_PARAM;
    if (mode != CAN_MODE_LOOPBACK_TEST && mode != CAN_MODE_NORMAL &&
        mode != CAN_MODE_SLEEP) return UNI_ERR_PARAM;
    return UNI_ERR_NOT_SUP;   /* 第2期: CANP_CTRL/CANC_SET_MODE */
}

int can_set_bitrate(can_ctx *ctx, uint8_t ch, uint32_t kbps)
{
    (void)ctx;
    if (ch > 1 || kbps < 5 || kbps > 1000) return UNI_ERR_PARAM;
    return UNI_ERR_NOT_SUP;   /* 第2期: CANP_CTRL/CANC_SET_BITRATE */
}

int can_send(can_ctx *ctx, uint8_t ch, const can_frame *f, unsigned timeout_ms)
{
    (void)ctx; (void)timeout_ms;
    if (!f || ch > 1 || f->dlc > 8) return UNI_ERR_PARAM;
    return UNI_ERR_NOT_SUP;   /* 第2期: CANP_TX + TXACK对账 */
}

int can_recv(can_ctx *ctx, uint8_t ch, can_frame *f, unsigned timeout_ms)
{
    (void)ctx; (void)timeout_ms;
    if (!f || ch > 1) return UNI_ERR_PARAM;
    return UNI_ERR_NOT_SUP;   /* 第2期: CANP_RX 分发 */
}

int can_flush_rx(can_ctx *ctx, uint8_t ch)
{
    (void)ctx;
    if (ch > 1) return UNI_ERR_PARAM;
    return UNI_ERR_NOT_SUP;
}

int can_get_status(can_ctx *ctx, uint8_t ch, can_ch_status *st)
{
    (void)ctx;
    if (!st || ch > 1) return UNI_ERR_PARAM;
    return UNI_ERR_NOT_SUP;   /* 第2期: CANC_GET_STATUS */
}

int can_get_info(can_ctx *ctx, uint8_t *proto_ver, uint8_t *ch_num)
{
    (void)ctx;
    if (!proto_ver || !ch_num) return UNI_ERR_PARAM;
    return UNI_ERR_NOT_SUP;   /* 第2期: CANC_GET_INFO */
}
