#ifndef __BSP_UART_H
#define __BSP_UART_H

#include "stdint.h"
#include "rtthread.h"
#include "chry_ringbuffer.h"

#ifdef __cplusplus
 extern "C" {
#endif

/* 缓存管理结构体 */
typedef struct {
    chry_ringbuffer_t rb_uart_rx;                     // 缓冲管理器
    struct rt_semaphore data_ready_sem;               // 数据就绪信号量
    struct rt_semaphore tx_complete_sem;              // 数据就绪信号量
    void(*write_completed_callback)(uint16_t);        // DMA发送完成回调
} bsp_uart_buffer_t;

void bsp_uart_config(uint32_t baudrate, uint8_t stopbits, uint8_t parity);

void bsp_uart_write(const uint8_t *data, uint32_t size);

void bsp_uart_write_by_dma(const uint8_t *data, uint16_t size);

void bsp_uart_wait_dma_write_completed();

#ifdef __cplusplus
 }
#endif

#endif 