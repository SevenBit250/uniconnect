#ifndef __USB2DEVICE_H
#define __USB2DEVICE_H

#include "ch32v30x.h"
#include <rtthread.h>

#ifdef __cplusplus
 extern "C" {
#endif

/* 乒乓缓存配置 - 每个缓冲区大小，总大小为双倍 */
#define UART_PINGPONG_BUFFER_SIZE 1024u
#define UART_DMA_RX_TOTAL_SIZE    (UART_PINGPONG_BUFFER_SIZE * 2)
#define UART_DMA_TX_BUFFER_SIZE   512u

/* 乒乓缓存状态结构体 */
typedef enum {
    PING_BUFFER_ACTIVE = 0,    // Ping缓冲区正在接收数据
    PONG_BUFFER_ACTIVE = 1     // Pong缓冲区正在接收数据
} pingpong_buffer_state_t;

/* 乒乓缓存管理结构体 */
typedef struct {
    uint8_t ping_buffer[UART_PINGPONG_BUFFER_SIZE];   // Ping缓冲区
    uint8_t pong_buffer[UART_PINGPONG_BUFFER_SIZE];   // Pong缓冲区

    pingpong_buffer_state_t active_buffer;           // 当前活跃的缓冲区
    uint32_t ping_received_count;                     // Ping缓冲区接收计数
    uint32_t pong_received_count;                     // Pong缓冲区接收计数

    rt_sem_t data_ready_sem;                          // 数据就绪信号量
    rt_mutex_t buffer_mutex;                          // 缓冲区访问互斥锁

    uint8_t *processing_buffer;                       // 当前正在处理的缓冲区指针
    uint32_t processing_length;                       // 当前处理的数据长度
} uart_pingpong_buffer_t;

/* 函数声明 */
void uart_perpare(void);
void uart_pingpong_init(void);
uint32_t uart_get_received_data(uint8_t **data_buffer);
void uart_release_processed_buffer(void);
void uart_start_high_speed_reception(void);
void uart_start_processing_thread(void);

#ifdef __cplusplus
}
#endif

#endif // __USB2DEVICE_H
