#include "usb2device.h"

/* 全局乒乓缓存管理实例 */
static uart_pingpong_buffer_t g_uart_pingpong;

/* DMA总缓冲区 - 包含ping和pong两个缓冲区 */
static uint8_t dma_total_buffer[UART_DMA_RX_TOTAL_SIZE];

/* 发送缓冲区 */
static uint8_t tx_buffer[UART_DMA_TX_BUFFER_SIZE];

/* 外部函数声明 */
extern void cdc_acm_data_send_with_dtr_test(uint8_t port);

/* 中断处理函数声明 */
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

/**
 * @brief 配置DMA为循环模式，用于乒乓缓存
 */
static inline void dma_pingpong_config(USART_TypeDef *uart, DMA_Channel_TypeDef *dma)
{
    DMA_InitTypeDef DMA_InitStructure = {0};

    /* 先停止DMA */
    DMA_Cmd(dma, DISABLE);
    DMA_DeInit(dma);

    /* 配置DMA为循环模式 */
    DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)(uart->DATAR);
    DMA_InitStructure.DMA_MemoryBaseAddr = (u32)dma_total_buffer;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;  /* 关键：循环模式 */
    DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_InitStructure.DMA_BufferSize = UART_DMA_RX_TOTAL_SIZE;
    DMA_Init(dma, &DMA_InitStructure);

    /* 使能半满传输中断和传输完成中断 */
    DMA_ITConfig(dma, DMA_IT_HT | DMA_IT_TC, ENABLE);

    /* 启动DMA */
    DMA_Cmd(dma, ENABLE);
}

/**
 * @brief 初始化乒乓缓存系统
 */
void uart_pingpong_init(void)
{
    /* 清零缓冲区 */
    rt_memset(&g_uart_pingpong, 0, sizeof(uart_pingpong_buffer_t));
    rt_memset(dma_total_buffer, 0, sizeof(dma_total_buffer));

    /* 创建信号量和互斥锁 */
    g_uart_pingpong.data_ready_sem = rt_sem_create("uart_data", 0, RT_IPC_FLAG_FIFO);
    g_uart_pingpong.buffer_mutex = rt_mutex_create("uart_buf", RT_IPC_FLAG_FIFO);

    /* 初始化状态 */
    g_uart_pingpong.active_buffer = PING_BUFFER_ACTIVE;
    g_uart_pingpong.processing_buffer = RT_NULL;
    g_uart_pingpong.processing_length = 0;

    rt_kprintf("UART Ping-Pong Buffer initialized: Total size %d bytes\n", UART_DMA_RX_TOTAL_SIZE);
}

/**
 * @brief 启动高速接收模式
 */
void uart_start_high_speed_reception(void)
{
    /* 确保系统已初始化 */
    if(g_uart_pingpong.data_ready_sem == RT_NULL) {
        uart_pingpong_init();
    }

    rt_kprintf("Starting high-speed UART reception with ping-pong buffering...\n");
}

/**
 * @brief 获取接收到的数据
 * @param data_buffer 输出参数，返回数据缓冲区指针
 * @return 接收到的数据长度
 */
uint32_t uart_get_received_data(uint8_t **data_buffer)
{
    uint32_t length = 0;

    if(g_uart_pingpong.processing_buffer != RT_NULL && g_uart_pingpong.processing_length > 0) {
        *data_buffer = g_uart_pingpong.processing_buffer;
        length = g_uart_pingpong.processing_length;
    }

    return length;
}

/**
 * @brief 释放已处理的缓冲区
 */
void uart_release_processed_buffer(void)
{
    g_uart_pingpong.processing_buffer = RT_NULL;
    g_uart_pingpong.processing_length = 0;
}

/**
 * @brief DMA中断处理函数 - 乒乓缓存核心逻辑
 */
void DMA1_Channel5_IRQHandler(void)
{
    /* 半满中断 - Ping缓冲区满了 */
    if(DMA_GetITStatus(DMA1_IT_HT5) != RESET) {
        DMA_ClearITPendingBit(DMA1_IT_HT5);

        /* 使用互斥锁保护共享数据 */
        if(rt_mutex_take(g_uart_pingpong.buffer_mutex, RT_WAITING_NO) == RT_EOK) {
            /* 将DMA的前半部分复制到Ping缓冲区 */
            rt_memcpy(g_uart_pingpong.ping_buffer,
                     dma_total_buffer,
                     UART_PINGPONG_BUFFER_SIZE);
            g_uart_pingpong.ping_received_count = UART_PINGPONG_BUFFER_SIZE;

            /* 设置处理缓冲区 */
            g_uart_pingpong.processing_buffer = g_uart_pingpong.ping_buffer;
            g_uart_pingpong.processing_length = UART_PINGPONG_BUFFER_SIZE;

            rt_mutex_release(g_uart_pingpong.buffer_mutex);

            /* 释放信号量通知应用线程 */
            rt_sem_release(g_uart_pingpong.data_ready_sem);

            /* 切换活跃缓冲区 */
            g_uart_pingpong.active_buffer = PONG_BUFFER_ACTIVE;
        }
    }

    /* 传输完成中断 - Pong缓冲区满了 */
    if(DMA_GetITStatus(DMA1_IT_TC5) != RESET) {
        DMA_ClearITPendingBit(DMA1_IT_TC5);

        /* 使用互斥锁保护共享数据 */
        if(rt_mutex_take(g_uart_pingpong.buffer_mutex, RT_WAITING_NO) == RT_EOK) {
            /* 将DMA的后半部分复制到Pong缓冲区 */
            rt_memcpy(g_uart_pingpong.pong_buffer,
                     &dma_total_buffer[UART_PINGPONG_BUFFER_SIZE],
                     UART_PINGPONG_BUFFER_SIZE);
            g_uart_pingpong.pong_received_count = UART_PINGPONG_BUFFER_SIZE;

            /* 设置处理缓冲区 */
            g_uart_pingpong.processing_buffer = g_uart_pingpong.pong_buffer;
            g_uart_pingpong.processing_length = UART_PINGPONG_BUFFER_SIZE;

            rt_mutex_release(g_uart_pingpong.buffer_mutex);

            /* 释放信号量通知应用线程 */
            rt_sem_release(g_uart_pingpong.data_ready_sem);

            /* 切换活跃缓冲区 */
            g_uart_pingpong.active_buffer = PING_BUFFER_ACTIVE;
        }
    }
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

    /* 初始化乒乓缓存系统 */
    uart_pingpong_init();

    /* 配置DMA为循环模式乒乓缓存 */
    dma_pingpong_config(USART1, DMA1_Channel5);

    /* 配置中断优先级 - 设置为高优先级确保快速响应 */
    nvic_config(DMA1_Channel5_IRQn);

    uart_config(USART1, 115200, 0, 0);

    /* 使能USART的DMA接收请求 */
    USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE);

    /* 启用USART */
    USART_Cmd(USART1, ENABLE);

    /* 启动高速接收模式 */
    uart_start_high_speed_reception();
}

/* DMA中断处理函数已在上面实现 */

/**
 * @brief 串口数据处理线程示例
 * 这个线程等待DMA信号量并处理接收到的数据
 */
static void uart_data_processing_thread(void *parameter)
{
    uint8_t *data_buffer;
    uint32_t data_length;

    rt_kprintf("UART Data Processing Thread Started\n");

    while(1) {
        /* 等待数据就绪信号量 */
        if(rt_sem_take(g_uart_pingpong.data_ready_sem, RT_WAITING_FOREVER) == RT_EOK) {

            /* 获取接收到的数据 */
            data_length = uart_get_received_data(&data_buffer);

            if(data_length > 0 && data_buffer != RT_NULL) {
                /* 这里可以添加你的数据处理逻辑 */
                rt_kprintf("Received %d bytes via ping-pong buffer\n", data_length);

                /* 示例：将数据转发到USB CDC */
                // cdc_acm_data_send(0, data_buffer, data_length);

                /* 处理完成后释放缓冲区 */
                uart_release_processed_buffer();
            }
        }
    }
}

/**
 * @brief 启动串口数据处理线程
 * 在main函数或app_entry中调用此函数来启动数据处理
 */
void uart_start_processing_thread(void)
{
    static rt_thread_t uart_thread = RT_NULL;

    if(uart_thread == RT_NULL) {
        uart_thread = rt_thread_create("uart_proc",
                                     uart_data_processing_thread,
                                     RT_NULL,
                                     2048,  /* 栈大小 */
                                     10,    /* 优先级 */
                                     10);   /* 时间片 */

        if(uart_thread != RT_NULL) {
            rt_thread_startup(uart_thread);
            rt_kprintf("UART processing thread created successfully\n");
        } else {
            rt_kprintf("Failed to create UART processing thread\n");
        }
    }
}
