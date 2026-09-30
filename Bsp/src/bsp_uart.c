#include "bsp_uart.h"
#include "bsp_config.h"
#include "ch32v30x.h"
#include <rtthread.h>
#include <rthw.h>
#include <string.h>

/***************************************************************************/
/*                                                                         */
/*            uart rx: 循环DMA乒乓双块 (uart -> usb)                       */
/*                                                                         */
/* Normal模式DMA块满后处于 EN=1/CNTR=0 静止态, 该系列CNTR=0等效65536,      */
/* 重臂窗口内到达的字节会让DMA从旧块首狂写数十KB涂烂全RAM(已由崩溃现场     */
/* 证实)。循环模式CNTR自动重载, DMA永不停转, 硬件级根治。                  */
/*                                                                         */
/* HT中断=块A满512B, TC中断=块B满512B: 满块零拷贝直入提交队列;             */
/* IDLE=部分块, 拷入对齐暂存槽(USBHS DMA要求4字节对齐)。                   */
/* 消费者(USB IN)不及时取走时乒乓块被绕回覆盖=丢数据, 与无流控语义一致。   */
/*                                                                         */
/***************************************************************************/

static __attribute__((aligned(4))) uint8_t uart_rx_pp[2][UART_RX_BLOCK_SIZE];      // 乒乓双块
static __attribute__((aligned(4))) uint8_t uart_rx_stage[2][UART_RX_BLOCK_SIZE];   // 部分块暂存槽
static volatile uint8_t uart_rx_stage_busy[2];
static volatile uint16_t uart_rx_consumed;            // 乒乓内已提交偏移 0..2*BLOCK

#define UART_RX_Q_LEN 16
#define RX_SEQ_PARTIAL 0xFFFFu   /* 暂存块是拷贝, 永不过期 */
static uart_rx_desc_t uart_rx_q[UART_RX_Q_LEN];
static volatile uint8_t uart_rx_q_rd, uart_rx_q_wt, uart_rx_q_cnt;
static volatile uint16_t uart_rx_seq;   /* 每次满块(HT/TC)递增, 用于过期校验 */

bsp_uart_buffer_t uart_handle;

/* 提交乒乓内 [from,to) 段 (ISR上下文, 关中断保护队列) */
static void bsp_uart_rx_submit(uint16_t from, uint16_t to)
{
    /* 不变式: 无论提交/丢弃, consumed必须推进到to, 否则DMA绕回后
     * pos<consumed会导致长度下溢的巨型memcpy */
    if (to <= from || (to - from) > 2 * UART_RX_BLOCK_SIZE) {
        g_dbg.submit_drop++;
        return;
    }
    uint16_t len = to - from;
    uint8_t half = (from >= UART_RX_BLOCK_SIZE) ? 1 : 0;
    uint16_t off = from - half * UART_RX_BLOCK_SIZE;
    uint8_t *src = &uart_rx_pp[half][off];

    rt_base_t level = rt_hw_interrupt_disable();
    uart_rx_desc_t *d = &uart_rx_q[uart_rx_q_wt];
    uint8_t staged = 0;
    if ((len == UART_RX_BLOCK_SIZE) && (off == 0)) {
        /* 满块: 乒乓块首恒对齐, 零拷贝; 记序号供pop时过期校验 */
        staged = 0;
    } else {
        /* 部分块: 拷入暂存槽 */
        uint8_t s = (uart_rx_stage_busy[0]) ? 1 : 0;
        if (uart_rx_stage_busy[s]) {
            uart_rx_consumed = to % (2 * UART_RX_BLOCK_SIZE);
            g_dbg.submit_drop++;
            rt_hw_interrupt_enable(level);
            return;
        }
        uart_rx_stage_busy[s] = 1;
        memcpy(uart_rx_stage[s], src, len);
        staged = 1 + s;
    }
    if (uart_rx_q_cnt >= UART_RX_Q_LEN) {
        /* 队列满: 满块直接丢(过期校验兜底), 暂存块需还槽 */
        if (staged) {
            uart_rx_stage_busy[staged - 1] = 0;
        }
        uart_rx_consumed = to % (2 * UART_RX_BLOCK_SIZE);
        uart_handle.rx_paused = 1;
        g_dbg.submit_drop++;
        rt_hw_interrupt_enable(level);
        return;
    }
    d->ptr = staged ? uart_rx_stage[staged - 1] : src;   /* 部分块发暂存槽副本 */
    d->len = len;
    d->staged = staged;
    d->seq = staged ? RX_SEQ_PARTIAL : ++uart_rx_seq;
    uart_rx_q_wt = (uart_rx_q_wt + 1) % UART_RX_Q_LEN;
    uart_rx_q_cnt++;
    uart_rx_consumed = to % (2 * UART_RX_BLOCK_SIZE);
    rt_hw_interrupt_enable(level);
    g_dbg.uart_submits++;
    rt_sem_release(&uart_handle.data_ready_sem);
}

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
    g_dbg.tx_dma++;
    if (size == 0) {
        /* 防御: 该系列DMA计数器写0等效65536, 绝不能以0启动传输 */
        g_dbg.tx_dma_zero++;
        rt_sem_release(&uart_handle.tx_complete_sem);
        return;
    }
    /* DMAT门控: 请求路径切断后再配置通道, TC后CNTR=0/EN=1窗口内TXE挂起
     * 不会触发65536计数狂读内存发线(该系列DMA与STM32行为不同) */
    USART1->CTLR3 &= ~USART_DMAReq_Tx;
    DMA_DeInit (DMA1_Channel4);
    while (DMA1_Channel4->CFGR & DMA_CFGR1_EN) {}
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
    DMA_ITConfig (DMA1_Channel4, DMA_IT_TC, ENABLE);
    DMA_Cmd (DMA1_Channel4, ENABLE);
    USART1->CTLR3 |= USART_DMAReq_Tx;
}

void bsp_uart_wait_dma_write_completed(void)
{
    // 等待dma发送完成
    rt_sem_take(&uart_handle.tx_complete_sem, RT_WAITING_FOREVER);
}

/***************************************************************************/
/*                        rx 消费接口                                       */
/***************************************************************************/

uint8_t bsp_uart_rx_pop(uart_rx_desc_t *desc)
{
    rt_base_t level = rt_hw_interrupt_disable();
    while (uart_rx_q_cnt) {
        uart_rx_desc_t *d = &uart_rx_q[uart_rx_q_rd];
        uart_rx_q_rd = (uart_rx_q_rd + 1) % UART_RX_Q_LEN;
        uart_rx_q_cnt--;
        if (d->seq != RX_SEQ_PARTIAL && (uint16_t)(uart_rx_seq - d->seq) >= 2) {
            /* 满块已被DMA绕回覆写(消费太慢), 丢弃避免发出未来数据 */
            g_dbg.recycle_drop++;
            continue;
        }
        *desc = *d;
        rt_hw_interrupt_enable(level);
        return 1;
    }
    rt_hw_interrupt_enable(level);
    return 0;
}

void bsp_uart_rx_release(uart_rx_desc_t *desc)
{
    if (desc->staged) {
        uart_rx_stage_busy[desc->staged - 1] = 0;   /* reset后重复release无害 */
        desc->staged = 0;
    }
}

void bsp_uart_rx_reset(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    uart_rx_q_rd = uart_rx_q_wt = uart_rx_q_cnt = 0;
    uart_rx_stage_busy[0] = uart_rx_stage_busy[1] = 0;
    uart_rx_consumed = 2 * UART_RX_BLOCK_SIZE - DMA_GetCurrDataCounter(DMA1_Channel5);
    uart_handle.rx_paused = 0;
    rt_hw_interrupt_enable(level);
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

    // rx dma: 循环模式乒乓双块, HT/TC中断各对应一块满, DMA永不停转
    DMA_DeInit (DMA1_Channel5);
    while (DMA1_Channel5->CFGR & DMA_CFGR1_EN) {}
    DMA_InitStructure.DMA_PeripheralInc                  = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc                      = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize             = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize                 = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode                           = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority                       = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_M2M                            = DMA_M2M_Disable;
    DMA_InitStructure.DMA_PeripheralBaseAddr             = (uint32_t)(&USART1->DATAR);
    DMA_InitStructure.DMA_MemoryBaseAddr                 = (uint32_t)uart_rx_pp;
    DMA_InitStructure.DMA_DIR                            = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize                     = 2 * UART_RX_BLOCK_SIZE;
    DMA_Init (DMA1_Channel5, &DMA_InitStructure);
    DMA_ITConfig (DMA1_Channel5, DMA_IT_TC | DMA_IT_HT, ENABLE);
    DMA_Cmd (DMA1_Channel5, ENABLE);
    /* TX的DMAT按传输门控(见write_by_dma/TC4), RX常开(循环模式安全) */
    USART_DMACmd (USART1, USART_DMAReq_Rx, ENABLE);

    return 0;
}
INIT_BOARD_EXPORT(bsp_uart_init);

static int bsp_uart_env_init(void)
{
    rt_sem_init(&uart_handle.data_ready_sem, "uart_rx", 0, RT_IPC_FLAG_FIFO);
    rt_sem_init(&uart_handle.tx_complete_sem, "uart_tx", 0, RT_IPC_FLAG_FIFO);
    return 0;
}
INIT_COMPONENT_EXPORT(bsp_uart_env_init);

/***************************************************************************/
/*                                                                         */
/*                              中断服务函数                                */
/*                                                                         */
/***************************************************************************/

void DMA1_Channel4_IRQHandler (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));
void DMA1_Channel4_IRQHandler (void) {
    rt_interrupt_enter();
    /* 第一时间切断TXE请求路径并清EN, 堵死CNTR=0/EN=1的65536狂读窗口 */
    USART1->CTLR3 &= ~USART_DMAReq_Tx;
    DMA1_Channel4->CFGR &= ~DMA_CFGR1_EN;
    DMA_ClearITPendingBit (DMA1_IT_TC4);
    rt_sem_release(&uart_handle.tx_complete_sem);
    rt_interrupt_leave();
}

void DMA1_Channel5_IRQHandler (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));
void DMA1_Channel5_IRQHandler (void) {
    rt_interrupt_enter();
    if (DMA_GetITStatus(DMA1_IT_HT5)) {
        DMA_ClearITPendingBit (DMA1_IT_HT5);
        g_dbg.uart_tc_evt++;                    // 满块事件计数
        if (uart_rx_consumed < UART_RX_BLOCK_SIZE) {
            /* 从当前消费点提交到半块边界(部分块已提交过前缀, 不能重发) */
            bsp_uart_rx_submit(uart_rx_consumed, UART_RX_BLOCK_SIZE);
        }
    }
    if (DMA_GetITStatus(DMA1_IT_TC5)) {
        DMA_ClearITPendingBit (DMA1_IT_TC5);
        g_dbg.uart_tc_evt++;
        /* HT分支在上方先行处理, 此时consumed必为512, 直接提交B半 */
        bsp_uart_rx_submit(UART_RX_BLOCK_SIZE, 2 * UART_RX_BLOCK_SIZE);
    }
    rt_interrupt_leave();
}

void USART1_IRQHandler (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));
void USART1_IRQHandler (void) {
    if (USART_GetITStatus(USART1, USART_IT_IDLE) != RESET){
        rt_interrupt_enter();
        USART_ReceiveData(USART1);
        g_dbg.uart_idle_evt++;
        uint16_t pos = 2 * UART_RX_BLOCK_SIZE - DMA_GetCurrDataCounter (DMA1_Channel5);
        if (pos != uart_rx_consumed) {
            bsp_uart_rx_submit(uart_rx_consumed, pos);
        } else {
            g_dbg.uart_idle_zero++;
        }
        rt_interrupt_leave();
    }
}
