#include "bsp_uart.h"
#include "bsp_config.h"
#include "ch32v30x.h"
#include <rtthread.h>
#include <rthw.h>
#include <string.h>

/***************************************************************************/
/*                                                                         */
/*        uart rx: 循环DMA 8KB环形缓冲 + 满块贪心收割 (uart -> usb)        */
/*                                                                         */
/* 设计: RX DMA循环模式落环(永不停转, 根治CNTR=0狂写); 满块提交由三类事件  */
/* 驱动: HT/TC里程碑中断、IDLE流尾、bulk_in完成后的主动收割 —— USB能发多  */
/* 快就收多快, 不依赖中断边界。环深=抖动容忍(8KB@921600≈89ms), 活跃读取  */
/* 下零丢失; 仅主机彻底停读才回绕丢弃(真无流控边界)。                      */
/* 账目用32位绝对位置(字节), 回绕由sync的圈数推进处理, 杜绝错位。          */
/*                                                                         */
/***************************************************************************/

static __attribute__((aligned(4))) uint8_t uart_rx_ring[UART_RX_RING_SIZE];
/* 暂存槽须容纳最大部分段(块尾<512, 块对齐起点, 不会跨环回绕) */
static __attribute__((aligned(4))) uint8_t uart_rx_stage[2][UART_RX_BLOCK_SIZE];
static volatile uint8_t uart_rx_stage_busy[2];

static volatile uint32_t uart_rx_abs;           /* DMA绝对写位置(字节, 单调) */
static volatile uint32_t uart_rx_consumed_abs;  /* 已提交绝对位置 */

#define UART_RX_Q_LEN 20
#define RX_ABS_PARTIAL 0xFFFFFFFFu   /* 部分块哨兵(永不过期, 数据在暂存槽) */
typedef struct {
    uint8_t *ptr;
    uint16_t len;
    uint8_t staged;
    uint32_t abs_start;              /* 满块起始绝对位置, 过期校验用 */
} uart_rx_qent_t;
static uart_rx_qent_t uart_rx_q[UART_RX_Q_LEN];
static volatile uint8_t uart_rx_q_rd, uart_rx_q_wt, uart_rx_q_cnt;

bsp_uart_buffer_t uart_handle;

/* 用当前CNTR刷新绝对写位置(处理环回绕, ISR/线程均可调) */
static void bsp_uart_rx_sync(void)
{
    uint16_t pos = UART_RX_RING_SIZE - DMA_GetCurrDataCounter(DMA1_Channel5);
    uint32_t last = uart_rx_abs;
    uint32_t base = last & ~(uint32_t)(UART_RX_RING_SIZE - 1);
    uint32_t abs = base + pos;
    if (pos < (last & (uint32_t)(UART_RX_RING_SIZE - 1))) {
        abs += UART_RX_RING_SIZE;   /* 跨圈 */
    }
    uart_rx_abs = abs;
}

/* 提交一个满块(绝对区间[start,start+512), 环内必然不跨回绕) */
static void bsp_uart_rx_push_full(uint32_t start)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (uart_rx_q_cnt >= UART_RX_Q_LEN) {
        uart_rx_consumed_abs = start + UART_RX_BLOCK_SIZE;   /* 队满丢块仍推进账目 */
        uart_handle.rx_paused = 1;
        g_dbg.submit_drop++;
        rt_hw_interrupt_enable(level);
        return;
    }
    uart_rx_qent_t *e = &uart_rx_q[uart_rx_q_wt];
    e->ptr = &uart_rx_ring[start & (UART_RX_RING_SIZE - 1)];
    e->len = UART_RX_BLOCK_SIZE;
    e->staged = 0;
    e->abs_start = start;
    uart_rx_q_wt = (uart_rx_q_wt + 1) % UART_RX_Q_LEN;
    uart_rx_q_cnt++;
    uart_rx_consumed_abs = start + UART_RX_BLOCK_SIZE;
    rt_hw_interrupt_enable(level);
    g_dbg.uart_submits++;
    rt_sem_release(&uart_handle.data_ready_sem);
}

/* 提交部分块(流尾, 拷入暂存槽) */
static void bsp_uart_rx_push_partial(uint32_t start, uint16_t len)
{
    rt_base_t level = rt_hw_interrupt_disable();
    uint8_t s = (uart_rx_stage_busy[0]) ? 1 : 0;
    if (uart_rx_stage_busy[s] || uart_rx_q_cnt >= UART_RX_Q_LEN) {
        uart_rx_consumed_abs = start + len;
        g_dbg.submit_drop++;
        rt_hw_interrupt_enable(level);
        return;
    }
    uart_rx_stage_busy[s] = 1;
    memcpy(uart_rx_stage[s], &uart_rx_ring[start & (UART_RX_RING_SIZE - 1)], len);
    uart_rx_qent_t *e = &uart_rx_q[uart_rx_q_wt];
    e->ptr = uart_rx_stage[s];
    e->len = len;
    e->staged = 1 + s;
    e->abs_start = RX_ABS_PARTIAL;
    uart_rx_q_wt = (uart_rx_q_wt + 1) % UART_RX_Q_LEN;
    uart_rx_q_cnt++;
    uart_rx_consumed_abs = start + len;
    rt_hw_interrupt_enable(level);
    g_dbg.uart_submits++;
    rt_sem_release(&uart_handle.data_ready_sem);
}

/* 收割: 任意相位的数据流按512硬边界切分.
 * 不变式: 只有完整落在硬边界上的段走零拷贝直引(USBHS要求4对齐);
 * 相位头(consumed非对齐到下一边界)与流尾(final)走partial拷贝.
 * 连续流仅首个相位头一次拷贝, 之后全满块零拷贝 */
void bsp_uart_rx_drain(uint8_t final)
{
    /* 全程关中断: drain会被线程(cdc_tx)与ISR(bulk_in/HT/TC/IDLE)交错调用,
     * 读-算-推序列若被打断会导致账目回退/跳块(设备实发与USB账目全对
     * 但环内整块消失的根因)。push内部的嵌套关中断无害 */
    rt_base_t level = rt_hw_interrupt_disable();
    bsp_uart_rx_sync();
    uint32_t pos = uart_rx_abs;
    uint32_t c = uart_rx_consumed_abs;
    uint32_t blk = UART_RX_BLOCK_SIZE;

    if (c & (blk - 1)) {
        /* 相位头: 补齐到下一个硬边界 */
        uint32_t nb = (c + blk - 1) & ~(blk - 1);
        if (pos >= nb) {
            bsp_uart_rx_push_partial(c, (uint16_t)(nb - c));
            c = uart_rx_consumed_abs;
        } else {
            if (final && pos > c) {
                bsp_uart_rx_push_partial(c, (uint16_t)(pos - c));
            }
            rt_hw_interrupt_enable(level);
            return;
        }
    }
    while (pos - c >= blk) {
        bsp_uart_rx_push_full(c);
        g_dbg.uart_tc_evt++;
        c = uart_rx_consumed_abs;
    }
    if (final && pos > c) {
        bsp_uart_rx_push_partial(c, (uint16_t)(pos - c));
    }
    rt_hw_interrupt_enable(level);
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
    g_dbg.tx_bytes += size;
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
        uart_rx_qent_t *e = &uart_rx_q[uart_rx_q_rd];
        uart_rx_q_rd = (uart_rx_q_rd + 1) % UART_RX_Q_LEN;
        uart_rx_q_cnt--;
        if (e->abs_start != RX_ABS_PARTIAL &&
            (uart_rx_abs - e->abs_start) >= (UART_RX_RING_SIZE - UART_RX_BLOCK_SIZE)) {
            /* 满块已被环回绕覆写(主机彻底停读), 丢弃避免发出旧数据 */
            g_dbg.recycle_drop++;
            continue;
        }
        desc->ptr = e->ptr;
        desc->len = e->len;
        desc->staged = e->staged;
        rt_hw_interrupt_enable(level);
        return 1;
    }
    rt_hw_interrupt_enable(level);
    return 0;
}

/* 合并弹出: 首块与队列中环内连续的满块合并成一次大传输(≤4KB).
 * 驱动内部按MPS链式分包, 显著减少IN完成回调/软件toggle翻转/ZLP次数,
 * 降低高速档toggle失配被主机静默丢包的概率 */
uint16_t bsp_uart_rx_pop_coalesce(uart_rx_desc_t *desc)
{
    if (!bsp_uart_rx_pop(desc)) {
        return 0;
    }
    if (desc->staged) {
        return desc->len;
    }
    rt_base_t level = rt_hw_interrupt_disable();
    uint16_t merged = desc->len;
    while (uart_rx_q_cnt && merged + UART_RX_BLOCK_SIZE <= 4096) {
        uart_rx_qent_t *e = &uart_rx_q[uart_rx_q_rd];
        if (e->staged || e->abs_start == RX_ABS_PARTIAL) {
            break;
        }
        /* 环内连续: 下一块指针 == 当前合并尾 */
        if (e->ptr != desc->ptr + merged) {
            break;
        }
        if (uart_rx_abs - e->abs_start >= (UART_RX_RING_SIZE - UART_RX_BLOCK_SIZE)) {
            break;   /* 下一块已过期, 不合并(留给pop丢弃) */
        }
        uart_rx_q_rd = (uart_rx_q_rd + 1) % UART_RX_Q_LEN;
        uart_rx_q_cnt--;
        merged += UART_RX_BLOCK_SIZE;
    }
    desc->len = merged;
    rt_hw_interrupt_enable(level);
    return merged;
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
    /* 清残留的HT/TC硬件标志, 防reset后陈旧满块事件按旧账目提交 */
    DMA_ClearITPendingBit(DMA1_IT_GL5);
    uart_rx_q_rd = uart_rx_q_wt = uart_rx_q_cnt = 0;
    uart_rx_stage_busy[0] = uart_rx_stage_busy[1] = 0;
    bsp_uart_rx_sync();
    uart_rx_consumed_abs = uart_rx_abs;   /* 任意相位, drain会自动补相位头partial */
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

    // tx dma nvic: 最高抢占优先级, TC4关闸(DMAT+EN)必须快于TXE上升
    NVIC_InitStructure.NVIC_IRQChannel                   = DMA1_Channel4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init (&NVIC_InitStructure);

    // rx dma: 循环模式落环, DMA永不停转
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
    DMA_InitStructure.DMA_MemoryBaseAddr                 = (uint32_t)uart_rx_ring;
    DMA_InitStructure.DMA_DIR                            = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize                     = UART_RX_RING_SIZE;
    DMA_Init (DMA1_Channel5, &DMA_InitStructure);
    /* HT/TC作圈中里程碑(收割兜底), 平时由消费端主动drain */
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
        bsp_uart_rx_drain(0);
    }
    if (DMA_GetITStatus(DMA1_IT_TC5)) {
        DMA_ClearITPendingBit (DMA1_IT_TC5);
        bsp_uart_rx_drain(0);
    }
    rt_interrupt_leave();
}

void USART1_IRQHandler (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));
void USART1_IRQHandler (void) {
    if (USART_GetITStatus(USART1, USART_IT_IDLE) != RESET){
        rt_interrupt_enter();
        USART_ReceiveData(USART1);
        g_dbg.uart_idle_evt++;
        bsp_uart_rx_drain(1);
        rt_interrupt_leave();
    }
}
