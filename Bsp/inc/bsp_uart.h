#ifndef __BSP_UART_H
#define __BSP_UART_H

#include "stdint.h"
#include "rtthread.h"
#include "bsp_pool.h"

#ifdef __cplusplus
 extern "C" {
#endif

/* RX提交描述符: staged=1表示数据在暂存槽中, 消费完成后必须release */
typedef struct {
    uint8_t *ptr;
    uint16_t len;
    uint8_t staged;
    uint16_t seq;    /* 满块序号(过期校验), 部分块为哨兵值 */
} uart_rx_desc_t;

/* UART缓存管理: RX走循环DMA乒乓双块(硬件永不停转, 根治CNTR=0狂写), TX走DMA+完成信号量 */
typedef struct {
    volatile uint8_t rx_paused;                      // 提交队列满标志(诊断)
    struct rt_semaphore data_ready_sem;              // RX数据就绪信号量
    struct rt_semaphore tx_complete_sem;             // TX完成信号量
} bsp_uart_buffer_t;

void bsp_uart_config(uint32_t baudrate, uint8_t stopbits, uint8_t parity);

void bsp_uart_write(const uint8_t *data, uint32_t size);

void bsp_uart_write_by_dma(const uint8_t *data, uint16_t size);

void bsp_uart_wait_dma_write_completed(void);

/* RX消费接口: 满块为乒乓块直引(零拷贝,块首恒4对齐), 部分块在对齐暂存槽 */
uint8_t bsp_uart_rx_pop(uart_rx_desc_t *desc);

/* 消费完成后归还(staged暂存槽), 幂等 */
void bsp_uart_rx_release(uart_rx_desc_t *desc);

/* 清空提交队列与暂存槽(波特率切换), 在途desc由release幂等兜底 */
void bsp_uart_rx_reset(void);

#ifdef __cplusplus
 }
#endif

#endif
