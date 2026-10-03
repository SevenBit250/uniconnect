/* can_comm.h -- UniConnect CAN 通讯库 (WinUSB bulk, 接口3, 双路复用)
 *
 * 模型: 一个 can_ctx 独占一台设备的 CAN 接口; 通道号 ch=0/1;
 * 收发为同步阻塞语义(timeout_ms=0 立即返回, UNI_ERR_TIMEOUT 表示无数据)。
 * 协议常量直引固件 common/can_proto.h(单一事实源)。
 */
#ifndef UNICONNECT_CAN_H
#define UNICONNECT_CAN_H

#include "uniconnect_common.h"
#include "can_proto.h"     /* 通道模式/CTRL命令/包类型常量 -- 固件同源 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct can_ctx can_ctx;   /* 不透明上下文 */

/* CAN帧(布局冻结): flags bit0=扩展ID, bit1=远程帧 */
typedef struct {
    uint32_t id;
    uint8_t  flags;
    uint8_t  dlc;
    uint8_t  data[8];
} can_frame;

/* 通道状态(GET_STATUS 应答, 布局冻结) */
typedef struct {
    uint32_t tx_cnt;
    uint32_t rx_cnt;
    uint32_t err_cnt;
} can_ch_status;

UNI_API uint32_t can_abi_version(void);
UNI_API const char *can_strerror(int err);

/* --- 生命周期 --- */
UNI_API int can_open(can_ctx **ctx);                    /* 枚举并独占CAN接口 */
UNI_API void can_close(can_ctx *ctx);

/* --- 配置(CLOSE/SLEEP态下设置; open后默认双路SLEEP) --- */
UNI_API int can_set_mode(can_ctx *ctx, uint8_t ch, uint8_t mode);      /* CAN_MODE_* */
UNI_API int can_set_bitrate(can_ctx *ctx, uint8_t ch, uint32_t kbps);  /* 5..1000 */

/* --- 收发 --- */
UNI_API int can_send(can_ctx *ctx, uint8_t ch, const can_frame *f,
                     unsigned timeout_ms);              /* 阻塞至TXACK或超时 */
UNI_API int can_recv(can_ctx *ctx, uint8_t ch, can_frame *f,
                     unsigned timeout_ms);              /* 阻塞至收到帧或超时 */
UNI_API int can_flush_rx(can_ctx *ctx, uint8_t ch);     /* 丢弃接收缓存 */

/* --- 状态 --- */
UNI_API int can_get_status(can_ctx *ctx, uint8_t ch, can_ch_status *st);
UNI_API int can_get_info(can_ctx *ctx, uint8_t *proto_ver, uint8_t *ch_num);

#ifdef __cplusplus
}
#endif

#endif /* UNICONNECT_CAN_H */
