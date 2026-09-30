#ifndef __BSP_CAN_H
#define __BSP_CAN_H

#include <stdint.h>

/* CAN帧描述(对齐4, 16B, 值拷贝语义) */
typedef struct {
    uint32_t id;        /* 11位标准ID或29位扩展ID */
    uint8_t  ide;       /* 0=标准帧 1=扩展帧 */
    uint8_t  rtr;       /* 0=数据帧 1=远程帧 */
    uint8_t  dlc;       /* 0-8 */
    uint8_t  _rsv;
    uint8_t  data[8];
} can_frame_t;

#define CAN_CH1  0
#define CAN_CH2  1
#define CAN_CH_NUM 2

/* 工作模式 */
#define CAN_MODE_LOOPBACK_TEST  0   /* 静默回环: 硬件回环+不驱动总线(自测) */
#define CAN_MODE_NORMAL         1   /* 正常收发(接总线) */

int bsp_can_init(uint8_t mode);

/* 非阻塞发送: 入发送环, TX空中断装填邮箱; 环满返回0(计can_tx_full) */
uint8_t bsp_can_send(uint8_t ch, const can_frame_t *f);

/* 取接收帧(消费者线程); 返回0=无帧 */
uint8_t bsp_can_rx_pop(uint8_t ch, can_frame_t *f);

/* 当前发送环剩余可入队帧数 */
uint8_t bsp_can_tx_free(uint8_t ch);

#endif
