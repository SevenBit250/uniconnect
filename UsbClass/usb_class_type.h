#ifndef __USB_CLASS_TYPE_H
#define __USB_CLASS_TYPE_H

#include "usb_cdc.h"
#include "rtthread.h"
#include "usb_class_def.h"
#include "chry_ringbuffer.h"

typedef struct {
    uint8_t cdc_buffer[USB_RB_RX_BUFFER_SIZE];
    uint8_t rb_buffer[USB_RB_RX_BUFFER_SIZE];        // 数据缓冲区 - usage of the thread
    chry_ringbuffer_t rb;                            // 缓冲管理器
    struct cdc_line_coding lincoding;                // 行设置
    rt_bool_t rx_buffer_full;                        // 接收缓冲满
    rt_bool_t tx_buffer_empty;                       // 发送缓冲空
    rt_bool_t allow_transfer;                        // 允许数据传输
    struct rt_semaphore data_ready_sem;              // 数据就绪信号量
    struct rt_semaphore lincoding_sem;               // 设备配置信号量 
} usb_cdc_buffer_t;

#endif
