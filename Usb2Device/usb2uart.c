#include "rtthread.h"
#include "usb2device.h"

static uart_buffer_t *uart_handle = RT_NULL;

static inline void uart_config (USART_TypeDef *uart, uint32_t baudrate, uint8_t stopbits, uint8_t parity) {
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

void uart_write_to_dev(const uint8_t *data, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        while(USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
        USART_SendData(USART1, *(data + i));
    }
}

void uart_writeln_to_dev(const char *str) {
    uint32_t size = rt_strlen(str);
    for (uint32_t i = 0; i < size; i++) {
        while(USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
        USART_SendData(USART1, *(str + i));
    }
}

void uart_config_override(uint32_t baudrate, uint8_t stopbits, uint8_t parity)
{
    uart_config(USART1, baudrate, stopbits, parity);
}

void uart_perpare (uart_buffer_t *handle) {
    DMA_InitTypeDef DMA_InitStructure = {0};
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    RT_ASSERT(handle);
    uart_handle = handle;

    // clock init
    RCC_AHBPeriphClockCmd (RCC_AHBPeriph_DMA1, ENABLE);
    RCC_APB2PeriphClockCmd (RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd (RCC_APB2Periph_USART1, ENABLE);

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
    uart_config (USART1, 115200, 0, 0);;
    USART_ITConfig (USART1, USART_IT_IDLE, ENABLE);
    NVIC_Init (&NVIC_InitStructure);
    USART_Cmd (USART1, ENABLE);

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
    DMA_InitStructure.DMA_MemoryBaseAddr                 = (uint32_t)uart_handle->dma_buffer;
    DMA_InitStructure.DMA_DIR                            = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize                     = sizeof (uart_handle->dma_buffer);
    NVIC_InitStructure.NVIC_IRQChannel                   = DMA1_Channel5_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    DMA_Init (DMA1_Channel5, &DMA_InitStructure);
    DMA_ITConfig (DMA1_Channel5, DMA_IT_TC, ENABLE);
    NVIC_Init (&NVIC_InitStructure);
    DMA_Cmd (DMA1_Channel5, ENABLE);
    USART_DMACmd (USART1, USART_DMAReq_Rx, ENABLE);

    // tx dma init
}

void DMA1_Channel5_IRQHandler (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));
void DMA1_Channel5_IRQHandler (void) {
    rt_interrupt_enter();
    DMA_Cmd(DMA1_Channel5, DISABLE);
    DMA_SetCurrDataCounter(DMA1_Channel5, sizeof (uart_handle->dma_buffer));
    DMA_Cmd (DMA1_Channel5, ENABLE);
    chry_ringbuffer_write (&uart_handle->rb, uart_handle->dma_buffer, sizeof (uart_handle->dma_buffer));
    rt_sem_release (&uart_handle->data_ready_sem);
    DMA_ClearITPendingBit (DMA1_IT_TC5);
    rt_interrupt_leave();
}

void USART1_IRQHandler (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));
void USART1_IRQHandler (void) {
    if (USART_GetITStatus(USART1, USART_IT_IDLE) != RESET){
        rt_interrupt_enter();
        uint16_t rxlen = (sizeof (uart_handle->dma_buffer) - DMA_GetCurrDataCounter (DMA1_Channel5));
        DMA_Cmd (DMA1_Channel5, DISABLE);
        DMA_SetCurrDataCounter (DMA1_Channel5, sizeof (uart_handle->dma_buffer));
        DMA_Cmd (DMA1_Channel5, ENABLE);
        USART_ReceiveData(USART1);
        chry_ringbuffer_write (&uart_handle->rb, uart_handle->dma_buffer, rxlen);
        rt_sem_release (&uart_handle->data_ready_sem);
        rt_interrupt_leave();
    }
}
