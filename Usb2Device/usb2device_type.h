#ifndef __USB2DEVICE_TYPE_H
#define __USB2DEVICE_TYPE_H

#include "rtthread.h"
#include "chry_ringbuffer.h"
#include "usb2device_def.h"

/* 缓存管理结构体 */
typedef struct {
    uint8_t rb_buffer[UART_RB_RX_BUFFER_SIZE];        // 数据缓冲区 - usage of the thread
    uint8_t dma_buffer[UART_DMA_RX_BUFFER_SIZE];      // MDA缓冲区 - usage of the dma half transfer interrupt
    chry_ringbuffer_t rb;                             // 缓冲管理器
    struct rt_semaphore data_ready_sem;               // 数据就绪信号量
} uart_buffer_t;

#endif