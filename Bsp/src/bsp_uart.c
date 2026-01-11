#include "bsp_uart.h"
#include "bsp_config.h"
#include "ch32v30x.h"
#include <rtthread.h>

static __attribute__((aligned(4))) uint8_t dma_rx_buffer[UART_DMA_RX_BUFFER_SIZE];           // uart rx dma buffer
static __attribute__((aligned(4))) uint8_t rb_uart_rx_buffer[UART_RB_RX_BUFFER_SIZE];        // ringbuffer for uart to host

bsp_uart_buffer_t uart_handle;

static uint16_t uart_tx_dma_size = 0;

static inline void bsp_uart_config_private (USART_TypeDef *uart, uint32_t baudrate, uint8_t stopbits, uint8_t parity) {
    USART_InitTypeDef USART_InitStructure = {0};

    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;

    /* Number of stop bits (0: 1 stop bit; 1: 1.5 stop bits; 2: 2 stop bits). */
    if (stopbits == 1) {
        USART_InitStructure.USART_StopBits = USART_StopBits_1_5;
    } else if (stopbits == 2) {
        USART_InitStructure.USART_StopBits = USART_StopBits_2;
    } else {
        USART_InitStructure.USART_StopBits = USART_StopBits_1;
    }

    /* Check digit (0: None; 1: Odd; 2: Even; 3: Mark; 4: Space); */
    if (parity == 1) {
        USART_InitStructure.USART_Parity = USART_Parity_Odd;
        USART_InitStructure.USART_WordLength = USART_WordLength_9b;
    } else if (parity == 2) {
        USART_InitStructure.USART_Parity = USART_Parity_Even;
        USART_InitStructure.USART_WordLength = USART_WordLength_9b;
    } else {
        USART_InitStructure.USART_Parity = USART_Parity_No;
        USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    }

    USART_Init (uart, &USART_InitStructure);
}

void bsp_uart_config(uint32_t baudrate, uint8_t stopbits, uint8_t parity)
{
    bsp_uart_config_private (USART1, baudrate, stopbits, parity);
}

void bsp_uart_write(const uint8_t *data, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        while(USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
        USART_SendData(USART1, *(data + i));
    }
}

void bsp_uart_write_by_dma(const uint8_t *data, uint16_t size)
{
    DMA_InitTypeDef DMA_InitStructure = {0};
    uart_tx_dma_size = size;
    DMA_DeInit (DMA1_Channel4);
    DMA_InitStructure.DMA_PeripheralInc                  = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc                      = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize             = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize                 = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode                           = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority                       = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_M2M                            = DMA_M2M_Disable;
    DMA_InitStructure.DMA_PeripheralBaseAddr             = (uint32_t)(&USART1->DATAR);
    DMA_InitStructure.DMA_MemoryBaseAddr                 = (uint32_t)data;
    DMA_InitStructure.DMA_DIR                            = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize                     = size;
    DMA_Init (DMA1_Channel4, &DMA_InitStructure);
    DMA_ITConfig(DMA1_Channel4, DMA_IT_TC, ENABLE);
    DMA_Cmd (DMA1_Channel4, ENABLE);
}

void bsp_uart_wait_dma_write_completed() 
{
    // 等待dma发送完成
    rt_sem_take(&uart_handle.tx_complete_sem, RT_WAITING_FOREVER);
}

static int bsp_uart_init(void)
{
    DMA_InitTypeDef DMA_InitStructure = {0};
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    // clock init
    RCC_AHBPeriphClockCmd (RCC_AHBPeriph_DMA1, ENABLE);
    RCC_APB2PeriphClockCmd (RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd (RCC_APB2Periph_USART1, ENABLE);

    // gpio init
    GPIO_InitStructure.GPIO_Pin                          = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed                        = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode                         = GPIO_Mode_AF_PP;
    GPIO_Init (GPIOA, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin                          = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode                         = GPIO_Mode_IPU;
    GPIO_Init (GPIOA, &GPIO_InitStructure);

    // uart init
    NVIC_InitStructure.NVIC_IRQChannel                   = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    bsp_uart_config_private (USART1, 115200, 0, 0);
    USART_ITConfig (USART1, USART_IT_IDLE, ENABLE);
    NVIC_Init (&NVIC_InitStructure);
    USART_Cmd (USART1, ENABLE);

    // rx dma nvic
    NVIC_InitStructure.NVIC_IRQChannel                   = DMA1_Channel5_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init (&NVIC_InitStructure);

    // tx dma nvic
    NVIC_InitStructure.NVIC_IRQChannel                   = DMA1_Channel4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init (&NVIC_InitStructure);

    // rx dma init
    DMA_DeInit (DMA1_Channel5);
    DMA_InitStructure.DMA_PeripheralInc                  = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc                      = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize             = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize                 = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode                           = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority                       = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_M2M                            = DMA_M2M_Disable;
    DMA_InitStructure.DMA_PeripheralBaseAddr             = (uint32_t)(&USART1->DATAR);
    DMA_InitStructure.DMA_MemoryBaseAddr                 = (uint32_t)dma_rx_buffer;
    DMA_InitStructure.DMA_DIR                            = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize                     = sizeof (dma_rx_buffer);
    DMA_Init (DMA1_Channel5, &DMA_InitStructure);
    DMA_ITConfig (DMA1_Channel5, DMA_IT_TC, ENABLE);
    DMA_Cmd (DMA1_Channel5, ENABLE);
    USART_DMACmd (USART1, USART_DMAReq_Rx | USART_DMAReq_Tx, ENABLE);

    return 0;
}
INIT_BOARD_EXPORT(bsp_uart_init);

static int bsp_uart_env_init(void)
{
    rt_sem_init(&uart_handle.data_ready_sem, "uart_rx", 0, RT_IPC_FLAG_FIFO);
    rt_sem_init(&uart_handle.tx_complete_sem, "uart_tx", 0, RT_IPC_FLAG_FIFO);
    chry_ringbuffer_init(&uart_handle.rb_uart_rx, rb_uart_rx_buffer, sizeof(rb_uart_rx_buffer));
    return 0;
}
INIT_COMPONENT_EXPORT(bsp_uart_env_init);

void DMA1_Channel4_IRQHandler (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));
void DMA1_Channel4_IRQHandler (void) {
    rt_interrupt_enter();
    DMA_ClearITPendingBit (DMA1_IT_TC4);
    if (uart_handle.write_completed_callback) {
        uart_handle.write_completed_callback(uart_tx_dma_size);
    }
    rt_interrupt_leave();
}

void DMA1_Channel5_IRQHandler (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));
void DMA1_Channel5_IRQHandler (void) {
    rt_interrupt_enter();
    DMA_Cmd(DMA1_Channel5, DISABLE);
    DMA_SetCurrDataCounter(DMA1_Channel5, sizeof (dma_rx_buffer));
    DMA_Cmd (DMA1_Channel5, ENABLE);
    chry_ringbuffer_write (&uart_handle.rb_uart_rx, dma_rx_buffer, sizeof (dma_rx_buffer));
    rt_sem_release (&uart_handle.data_ready_sem);
    DMA_ClearITPendingBit (DMA1_IT_TC5);
    rt_interrupt_leave();
}

void USART1_IRQHandler (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));
void USART1_IRQHandler (void) {
    if (USART_GetITStatus(USART1, USART_IT_IDLE) != RESET){
        rt_interrupt_enter();
        uint16_t rxlen = (sizeof (dma_rx_buffer) - DMA_GetCurrDataCounter (DMA1_Channel5));
        DMA_Cmd (DMA1_Channel5, DISABLE);
        DMA_SetCurrDataCounter (DMA1_Channel5, sizeof (dma_rx_buffer));
        DMA_Cmd (DMA1_Channel5, ENABLE);
        USART_ReceiveData(USART1);
        chry_ringbuffer_write (&uart_handle.rb_uart_rx, dma_rx_buffer, rxlen);
        rt_sem_release (&uart_handle.data_ready_sem);
        rt_interrupt_leave();
    }
}
