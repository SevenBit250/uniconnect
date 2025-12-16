#include "rtthread.h"
#include "chry_ringbuffer.h"

/* ringbuffer缓冲区 */
#define UART_RB_RX_BUFFER_SIZE 1024u
/* dma rx缓冲区 */
#define UART_DMA_RX_BUFFER_SIZE   512u
/* dma tx缓冲区 */
#define UART_DMA_TX_BUFFER_SIZE   255u

/* 缓存管理结构体 */
typedef struct {
    uint8_t rb_buffer[UART_RB_RX_BUFFER_SIZE];        // 数据缓冲区 - usage of the thread
    uint8_t dma_buffer[UART_DMA_RX_BUFFER_SIZE];      // MDA缓冲区 - usage of the dma half transfer interrupt
    chry_ringbuffer_t rb;                             // 缓冲管理器
    rt_sem_t data_ready_sem;                          // 数据就绪信号量
    rt_bool_t is_uart_writing_to_usb;                 // 是否正在写数据到usb
} uart_buffer_t;