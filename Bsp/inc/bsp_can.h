#ifndef __BSP_CAN_H
#define __BSP_CAN_H

#include <stdint.h>

/* CAN帧描述(对齐4, 20B, 值拷贝语义) */
typedef struct {
    uint32_t id;        /* 11位标准ID或29位扩展ID */
    uint8_t  flags;     /* bit0:ide bit1:rtr */
    uint8_t  dlc;       /* 0-8 */
    uint16_t handle;    /* 主机侧回执句柄(TX方向), 设备原样带回 */
    uint8_t  data[8];
    uint32_t ts_us;     /* RX: 1µs时间戳(自由运行, ~71min回绕) */
} can_frame_t;

#define CANF_IDE  0x01
#define CANF_RTR  0x02

/* TX回执 */
typedef struct {
    uint16_t handle;
    uint8_t  ch;
    uint8_t  status;    /* CAN_ACK_* */
} can_ack_t;

#define CAN_ACK_SENT      0
#define CAN_ACK_ARB_LOST  1
#define CAN_ACK_ERROR     2
#define CAN_ACK_ABORT     3   /* 停止/模式切换时被放弃 */
#define CAN_ACK_DROPPED   4   /* 发送环满丢弃 */

#define CAN_CH1  0
#define CAN_CH2  1
#define CAN_CH_NUM 2

/* 工作模式 */
#define CAN_MODE_LOOPBACK_TEST  0   /* 静默回环: 硬件回环+不驱动总线(自测) */
#define CAN_MODE_NORMAL         1   /* 正常收发(接总线) */
#define CAN_MODE_SLEEP          2   /* 停止(睡眠, 不收不发) */

int bsp_can_init(uint8_t mode);
int bsp_can_set_bitrate(uint8_t ch, uint32_t kbps);
int bsp_can_set_mode(uint8_t ch, uint8_t mode);
uint32_t bsp_can_get_bitrate(uint8_t ch);
uint8_t bsp_can_get_mode(uint8_t ch);

/* 非阻塞发送: 入发送环, TX空中断装填邮箱; 环满返回0(应答CAN_ACK_DROPPED) */
uint8_t bsp_can_send(uint8_t ch, const can_frame_t *f);

/* 取接收帧(消费者线程); 返回0=无帧 */
uint8_t bsp_can_rx_pop(uint8_t ch, can_frame_t *f);

/* 取TX回执; 返回0=无 */
uint8_t bsp_can_ack_pop(can_ack_t *a);
/* 线程侧回执入环(桥接层DROPPED回执用) */
void bsp_can_ack_push(can_ack_t *a);

/* 当前发送环剩余可入队帧数 */
uint8_t bsp_can_tx_free(uint8_t ch);

/* 1µs自由运行时间戳(TIM3+回绕影子) */
uint32_t bsp_can_now_us(void);

/* 数据活动信号量(rx帧/回执到达时释放), 桥接线程等待 */
struct rt_semaphore;
struct rt_semaphore *bsp_can_activity_sem(void);

/* ---- CAN-USB 桥接协议 (WinUSB bulk, 单接口双路复用) ----
 * 包头8B(LE): [type:1][ch:1][seq:2][count:2][rsv:2], 记录4字节对齐 */
#define CANP_TX        0x01  /* H->D: count帧, 16B记录: id:4 flags:1 dlc:1 handle:2 data:8 */
#define CANP_CTRL      0x02  /* H->D: [cmd:1][payload...] */
#define CANP_SOFTRESET 0x7F  /* H->D: 链路软复位 */
#define CANP_RX        0x11  /* D->H: count帧, 20B记录: id:4 flags:1 dlc:1 rsv:1 ts:4 data:8 */
#define CANP_TXACK     0x13  /* D->H: count条, 4B记录: handle:2 status:1 ch:1 */
#define CANP_EVENT     0x14  /* D->H: count条, 8B记录: ev:1 rsv:1 value:4 rsv:2 */
#define CANP_CTRL_RSP  0x15  /* D->H: [cmd:1][status:1][payload...] */

/* CTRL命令 */
#define CANC_GET_INFO    0x01  /* rsp: proto:1 ch_num:1 rsv:2 */
#define CANC_SET_BITRATE 0x02  /* payload: u32 kbps */
#define CANC_SET_MODE    0x03  /* payload: u8 mode */
#define CANC_GET_STATUS  0x04  /* rsp: 每路tx/rx/err u32x3 */

/* 事件码 */
#define CANE_RX_OVF      0x01  /* value=累计丢弃帧数 */
#define CANE_BUS_OFF     0x02
#define CANE_ERR_PASSIVE 0x03

#define CAN_PROTO_VER    1

#endif
