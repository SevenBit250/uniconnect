#ifndef __BSP_UART_H
#define __BSP_UART_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stdint.h"
#include "rtthread.h"
#include "chry_ringbuffer.h"

/* ringbuffer缓冲区 */
#define UART_RB_RX_BUFFER_SIZE 1024u
/* dma rx缓冲区 */
#define UART_DMA_RX_BUFFER_SIZE   512u

/* 缓存管理结构体 */
typedef struct {
    uint8_t rb_buffer[UART_RB_RX_BUFFER_SIZE];        // 数据缓冲区
    uint8_t dma_buffer[UART_DMA_RX_BUFFER_SIZE];      // DMA缓冲区
    chry_ringbuffer_t rb;                             // 缓冲管理器
    struct rt_semaphore data_ready_sem;               // 数据就绪信号量
    struct rt_semaphore tx_complete_sem;               // 数据就绪信号量
    void(*dma_tx_complate_callback)(uint16_t);        // DMA发送完成回调
} uart_buffer_t;

void bsp_uart_config(uint32_t baudrate, uint8_t stopbits, uint8_t parity);

void bsp_uart_write(const uint8_t *data, uint32_t size);

void bsp_uart_write_by_dma(const uint8_t *data, uint16_t size);

void bsp_uart_wait_tx_dma_completed();

#ifdef __cplusplus
 }
#endif

#endif 