#include "usb2device.h"

uint8_t rx_buffer[UART_DMA_RX_BUFFER_SIZE];

uint8_t tx_buffer[UART_DMA_TX_BUFFER_SIZE];

void DMA1_Channel5_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void uart_config(USART_TypeDef *uart, uint32_t baudrate, uint8_t stopbits, uint8_t parity)
{
    USART_InitTypeDef USART_InitStructure = {0};

    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;

    /* Number of stop bits (0: 1 stop bit; 1: 1.5 stop bits; 2: 2 stop bits). */
    if(stopbits == 1) {
        USART_InitStructure.USART_StopBits = USART_StopBits_1_5;
    }
    else if(stopbits == 2) {
        USART_InitStructure.USART_StopBits = USART_StopBits_2;
    }
    else {
        USART_InitStructure.USART_StopBits = USART_StopBits_1;
    }

    /* Check digit (0: None; 1: Odd; 2: Even; 3: Mark; 4: Space); */
    if(parity == 1) {
        USART_InitStructure.USART_Parity = USART_Parity_Odd;
        USART_InitStructure.USART_WordLength = USART_WordLength_9b;
    }
    else if(parity == 2) {
        USART_InitStructure.USART_Parity = USART_Parity_Even;
        USART_InitStructure.USART_WordLength = USART_WordLength_9b;
    }
    else {
        USART_InitStructure.USART_Parity = USART_Parity_No;
        USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    }

    USART_Init(uart, &USART_InitStructure);
}

__attribute__((weak)) void uart_rcc_config_callback(void)
{
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    RCC_APB1PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
}

static inline void dma_config(USART_TypeDef *uart, DMA_Channel_TypeDef *dma, uint8_t *rxb_addr, uint32_t rxb_size, uint8_t *txb_addr, uint32_t txb_size)
{
    DMA_InitTypeDef DMA_InitStructure = {0};
    DMA_DeInit(dma);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)(uart->DATAR);
    DMA_InitStructure.DMA_MemoryBaseAddr = (u32)rxb_addr;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_InitStructure.DMA_BufferSize = rxb_size;
    DMA_Init(dma, &DMA_InitStructure);
    DMA_ITConfig(dma, DMA_IT_HT, ENABLE);
    DMA_Cmd(dma, ENABLE);
}

static inline void nvic_config(uint32_t interrupt_number)
{
    NVIC_InitTypeDef  NVIC_InitStructure = {0};
    NVIC_InitStructure.NVIC_IRQChannel = interrupt_number;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

static inline void gpio_config(GPIO_TypeDef *tx_prot, uint16_t tx_pin, GPIO_TypeDef *rx_prot, uint16_t rx_pin)
{
    GPIO_InitTypeDef  GPIO_InitStructure = {0};

    GPIO_InitStructure.GPIO_Pin   = tx_pin;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(tx_prot, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin   = rx_pin;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(rx_prot, &GPIO_InitStructure);
}

void uart_perpare(void)
{
    uart_rcc_config_callback();

    gpio_config(GPIOA, GPIO_Pin_9, GPIOA, GPIO_Pin_10);

    dma_config(USART1, DMA1_Channel5, rx_buffer, sizeof(rx_buffer), NULL, 0);

    nvic_config(DMA1_Channel5_IRQn);
    
    uart_config(USART1, 115200, 0, 0);

    USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE);

    USART_Cmd(USART1, ENABLE);
}

void DMA1_Channel5_IRQHandler(void)
{

    DMA_ClearITPendingBit(DMA1_IT_HT5);
}
