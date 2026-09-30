#include "ch32v30x.h"
#include <rtthread.h>
#include <rthw.h>
#include <string.h>
#include "bsp_can.h"
#include "bsp_config.h"
#include "bsp_pool.h"      /* g_dbg */

/* TXMIR位定义(RM 24.7.11: 库头文件未提供宏) */
#define CAN_TXMIR_TXRQ   (1u << 0)
#define CAN_TXMIR_RTR    (1u << 1)
#define CAN_TXMIR_IDE    (1u << 2)

/* 双路CAN驱动: bxCAN邮箱/FIFO架构(无DMA请求线), FIFO0 FMP中断收帧入环,
 * TX空中断从发送环续装邮箱。帧环SPSC(线程侧关中断, ISR侧天然独占)。 */

typedef struct {
    can_frame_t buf[CAN_TX_RING_LEN];
    volatile uint8_t head;
    volatile uint8_t tail;
} can_tx_ring_t;

typedef struct {
    can_frame_t buf[CAN_RX_RING_LEN];
    volatile uint8_t head;
    volatile uint8_t tail;
} can_rx_ring_t;

typedef struct {
    CAN_TypeDef *reg;
    can_tx_ring_t tx;
    can_rx_ring_t rx;
} can_chan_t;

static can_chan_t can_chan[CAN_CH_NUM];

static uint8_t tx_ring_push(can_tx_ring_t *r, const can_frame_t *f)
{
    uint8_t nx = (r->head + 1) % CAN_TX_RING_LEN;
    if (nx == r->tail) {
        return 0;
    }
    r->buf[r->head] = *f;
    r->head = nx;
    return 1;
}

static uint8_t tx_ring_pop_isr(can_tx_ring_t *r, can_frame_t *f)
{
    if (r->head == r->tail) {
        return 0;
    }
    *f = r->buf[r->tail];
    r->tail = (r->tail + 1) % CAN_TX_RING_LEN;
    return 1;
}

static uint8_t rx_ring_push_isr(can_rx_ring_t *r, const can_frame_t *f)
{
    uint8_t nx = (r->head + 1) % CAN_RX_RING_LEN;
    if (nx == r->tail) {
        return 0;
    }
    r->buf[r->head] = *f;
    r->head = nx;
    return 1;
}

uint8_t bsp_can_rx_pop(uint8_t ch, can_frame_t *f)
{
    if (ch >= CAN_CH_NUM) return 0;
    can_rx_ring_t *r = &can_chan[ch].rx;
    rt_base_t level = rt_hw_interrupt_disable();
    if (r->head == r->tail) {
        rt_hw_interrupt_enable(level);
        return 0;
    }
    *f = r->buf[r->tail];
    r->tail = (r->tail + 1) % CAN_RX_RING_LEN;
    rt_hw_interrupt_enable(level);
    return 1;
}

uint8_t bsp_can_tx_free(uint8_t ch)
{
    if (ch >= CAN_CH_NUM) return 0;
    can_tx_ring_t *r = &can_chan[ch].tx;
    uint8_t used;
    rt_base_t level = rt_hw_interrupt_disable();
    used = (uint8_t)((r->head + CAN_TX_RING_LEN - r->tail) % CAN_TX_RING_LEN);
    rt_hw_interrupt_enable(level);
    return (uint8_t)(CAN_TX_RING_LEN - 1 - used);
}

/* 把帧装入一个空闲邮箱(直接寄存器写, ISR/线程均可调; 无空闲邮箱返回0) */
static uint8_t can_load_mailbox(CAN_TypeDef *CANx, const can_frame_t *f)
{
    uint8_t mbox;
    uint32_t wr;

    if (CANx->TSTATR & CAN_TSTATR_TME0) {
        mbox = 0;
    } else if (CANx->TSTATR & CAN_TSTATR_TME1) {
        mbox = 1;
    } else if (CANx->TSTATR & CAN_TSTATR_TME2) {
        mbox = 2;
    } else {
        return 0;
    }

    /* 标准帧: EXID=0, IDE=0; 扩展帧: EXID=id[28:0], IDE=1 (STID承载id[28:18]) */
    if (f->ide) {
        CANx->sTxMailBox[mbox].TXMIR = ((f->id & 0x1FFFFFFFu) << 3) | CAN_TXMIR_IDE;
    } else {
        CANx->sTxMailBox[mbox].TXMIR = ((f->id & 0x7FFu) << 21);
    }
    if (f->rtr) {
        CANx->sTxMailBox[mbox].TXMIR |= CAN_TXMIR_RTR;
    }
    CANx->sTxMailBox[mbox].TXMDTR = f->dlc & 0x0F;
    memcpy(&wr, &f->data[0], 4);
    CANx->sTxMailBox[mbox].TXMDLR = wr;
    memcpy(&wr, &f->data[4], 4);
    CANx->sTxMailBox[mbox].TXMDHR = wr;
    CANx->sTxMailBox[mbox].TXMIR |= CAN_TXMIR_TXRQ;   /* 请求发送 */
    return 1;
}

/* 唯一装填点: 从发送环弹出帧装入空闲邮箱, 直到环空或邮箱满。
 * 线程侧(关中断内)与TX空中断共用, 保证每帧只装一次 */
static void can_tx_drain(uint8_t ch)
{
    can_chan_t *c = &can_chan[ch];
    can_frame_t f;
    while (tx_ring_pop_isr(&c->tx, &f)) {
        if (!can_load_mailbox(c->reg, &f)) {
            /* 邮箱满: 该帧已被弹出, 放回环头 */
            uint8_t t = (uint8_t)((c->tx.tail + CAN_TX_RING_LEN - 1) % CAN_TX_RING_LEN);
            c->tx.buf[t] = f;
            c->tx.tail = t;
            break;
        }
        if (ch == CAN_CH1) g_dbg.can1_tx++; else g_dbg.can2_tx++;
    }
}

/* TX空中断: 邮箱腾空后续装发送环中的帧, 保持流水 */
static void can_tx_isr(uint8_t ch)
{
    CAN_ClearITPendingBit(can_chan[ch].reg, CAN_IT_TME);
    can_tx_drain(ch);
}

/* RX0中断: FIFO0挂起帧全部搬入接收环 */
static void can_rx0_isr(uint8_t ch)
{
    can_chan_t *c = &can_chan[ch];
    while (CAN_MessagePending(c->reg, CAN_FIFO0)) {
        can_frame_t f;
        CanRxMsg msg;
        CAN_Receive(c->reg, CAN_FIFO0, &msg);
        f.ide = (msg.IDE == CAN_Id_Extended);
        f.rtr = (msg.RTR == CAN_RTR_Remote);
        f.dlc = msg.DLC & 0x0F;
        f.id = f.ide ? msg.ExtId : msg.StdId;
        memcpy(f.data, msg.Data, 8);
        if (ch == CAN_CH1) g_dbg.can1_rx++; else g_dbg.can2_rx++;
        if (!rx_ring_push_isr(&c->rx, &f)) {
            g_dbg.can_rx_drop++;
        }
    }
    CAN_ClearITPendingBit(c->reg, CAN_IT_FMP0);
}

uint8_t bsp_can_send(uint8_t ch, const can_frame_t *f)
{
    if (ch >= CAN_CH_NUM) return 0;
    can_chan_t *c = &can_chan[ch];
    rt_base_t level = rt_hw_interrupt_disable();
    uint8_t ok = tx_ring_push(&c->tx, f);
    if (ok) {
        can_tx_drain(ch);   /* 邮箱空闲则立即装填, 否则等TX空中断续装 */
    }
    rt_hw_interrupt_enable(level);
    if (!ok) {
        g_dbg.can_tx_full++;
    }
    return ok;
}

/* ---- 中断入口 ---- */
void USB_HP_CAN1_TX_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USB_HP_CAN1_TX_IRQHandler(void)
{
    rt_interrupt_enter();
    can_tx_isr(CAN_CH1);
    rt_interrupt_leave();
}

void USB_LP_CAN1_RX0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USB_LP_CAN1_RX0_IRQHandler(void)
{
    rt_interrupt_enter();
    can_rx0_isr(CAN_CH1);
    rt_interrupt_leave();
}

void CAN2_TX_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void CAN2_TX_IRQHandler(void)
{
    rt_interrupt_enter();
    can_tx_isr(CAN_CH2);
    rt_interrupt_leave();
}

void CAN2_RX0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void CAN2_RX0_IRQHandler(void)
{
    rt_interrupt_enter();
    can_rx0_isr(CAN_CH2);
    rt_interrupt_leave();
}

/* ---- 初始化 ---- */

/* 位时序: 固定18tq (SJW=1 + BS1=13 + BS2=4, 采样点77.8%),
 * 预分频 P = PCLK1 / (bitrate*18)。1M@72MHz: P=4; 500k: P=8; ... */
int bsp_can_init(uint8_t mode)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    CAN_InitTypeDef CAN_InitStructure = {0};
    CAN_FilterInitTypeDef CAN_FilterInitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    uint32_t bitrate = CAN_BITRATE_KBPS * 1000u;
    uint32_t pclk1 = 72000000u;   /* 144MHz HCLK / PPRE1_DIV2 */
    uint32_t brp = pclk1 / (bitrate * 18u);
    if (brp < 1 || brp > 1024 || pclk1 % (bitrate * 18u) != 0) {
        return -1;   /* 该波特率无法整除出18tq时序 */
    }

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1 | RCC_APB1Periph_CAN2, ENABLE);

    /* CAN1: PB9=TX(AF_PP) PB8=RX(IPU), Remap1(PD0/PD1在48脚封装不存在) */
    GPIO_PinRemapConfig(GPIO_Remap1_CAN1, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* CAN2: PB13=TX PB12=RX(固定映射) */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    memset(can_chan, 0, sizeof(can_chan));
    can_chan[CAN_CH1].reg = CAN1;
    can_chan[CAN_CH2].reg = CAN2;

    CAN_InitStructure.CAN_TTCM = DISABLE;
    CAN_InitStructure.CAN_ABOM = ENABLE;    /* 总线脱离自动恢复 */
    CAN_InitStructure.CAN_AWUM = DISABLE;
    CAN_InitStructure.CAN_NART = DISABLE;   /* 仲裁失败/出错自动重发(CAN标准行为) */
    CAN_InitStructure.CAN_RFLM = DISABLE;   /* FIFO满新帧覆盖旧帧 */
    CAN_InitStructure.CAN_TXFP = ENABLE;    /* 按请求顺序发送 */
    CAN_InitStructure.CAN_Mode = (mode == CAN_MODE_LOOPBACK_TEST)
                                     ? CAN_Mode_Silent_LoopBack
                                     : CAN_Mode_Normal;
    CAN_InitStructure.CAN_SJW = CAN_SJW_1tq;
    CAN_InitStructure.CAN_BS1 = CAN_BS1_13tq;
    CAN_InitStructure.CAN_BS2 = CAN_BS2_4tq;
    CAN_InitStructure.CAN_Prescaler = brp;

    /* 注意: WCH库CAN_Init成功返回1失败返回0(STM32风格), 不能按真值判断
     * (曾因if(ret)把成功当失败, CAN1后早退: CAN2未初始化+中断未使能) */
    uint8_t ret = CAN_Init(CAN1, &CAN_InitStructure);
    if (ret != CAN_InitStatus_Success) return -2;
    ret = CAN_Init(CAN2, &CAN_InitStructure);
    if (ret != CAN_InitStatus_Success) return -3;

    /* 过滤器: 28组bank, 前14归CAN1后14归CAN2, 各用1组32位掩码全收 → FIFO0 */
    CAN_SlaveStartBank(14);

    CAN_FilterInitStructure.CAN_FilterNumber = 0;
    CAN_FilterInitStructure.CAN_FilterMode = CAN_FilterMode_IdMask;
    CAN_FilterInitStructure.CAN_FilterScale = CAN_FilterScale_32bit;
    CAN_FilterInitStructure.CAN_FilterIdHigh = 0;
    CAN_FilterInitStructure.CAN_FilterIdLow = 0;
    CAN_FilterInitStructure.CAN_FilterMaskIdHigh = 0;
    CAN_FilterInitStructure.CAN_FilterMaskIdLow = 0;
    CAN_FilterInitStructure.CAN_FilterFIFOAssignment = CAN_Filter_FIFO0;
    CAN_FilterInitStructure.CAN_FilterActivation = ENABLE;
    CAN_FilterInit(&CAN_FilterInitStructure);   /* bank0-13归CAN1 */

    CAN_FilterInitStructure.CAN_FilterNumber = 14;
    CAN_FilterInit(&CAN_FilterInitStructure);   /* bank14-27归CAN2 */

    /* 中断: RX0/TX, 抢占优先级2(低于USBHS=1/TC4=0) */
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;

    NVIC_InitStructure.NVIC_IRQChannel = USB_LP_CAN1_RX0_IRQn;
    NVIC_Init(&NVIC_InitStructure);
    NVIC_InitStructure.NVIC_IRQChannel = USB_HP_CAN1_TX_IRQn;
    NVIC_Init(&NVIC_InitStructure);
    NVIC_InitStructure.NVIC_IRQChannel = CAN2_RX0_IRQn;
    NVIC_Init(&NVIC_InitStructure);
    NVIC_InitStructure.NVIC_IRQChannel = CAN2_TX_IRQn;
    NVIC_Init(&NVIC_InitStructure);

    CAN_ITConfig(CAN1, CAN_IT_FMP0 | CAN_IT_TME, ENABLE);
    CAN_ITConfig(CAN2, CAN_IT_FMP0 | CAN_IT_TME, ENABLE);
    return 0;
}

static int bsp_can_board_init(void)
{
    /* 驱动起来阶段: 静默回环自测模式(不驱动总线); 接总线时改CAN_MODE_NORMAL */
    return bsp_can_init(CAN_MODE_LOOPBACK_TEST);
}
INIT_BOARD_EXPORT(bsp_can_board_init);
