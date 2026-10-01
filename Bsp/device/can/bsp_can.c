#include "ch32v30x.h"
#include <rtthread.h>
#include <rthw.h>
#include <string.h>
#include "bsp_can.h"
#include "bsp_config.h"
#include "bsp_pool.h"      /* g_dbg */

/* 双路CAN驱动: bxCAN邮箱/FIFO架构(无DMA请求线), FIFO0 FMP中断收帧入环,
 * TX空中断从发送环续装邮箱。帧环SPSC(线程侧关中断, ISR侧天然独占)。
 * TX回执: 每邮箱跟踪handle, RQCP完成位触发TX-ACK; RX帧带1µs时间戳(TIM3)。 */

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

#define CAN_ACK_RING_LEN 16
typedef struct {
    can_ack_t buf[CAN_ACK_RING_LEN];
    volatile uint8_t head;
    volatile uint8_t tail;
} can_ack_ring_t;

typedef struct {
    CAN_TypeDef *reg;
    can_tx_ring_t tx;
    can_rx_ring_t rx;
    uint16_t mbox_handle[3];   /* 邮箱在途帧的handle, 0xFFFF=空 */
} can_chan_t;

static can_chan_t can_chan[CAN_CH_NUM];
static can_ack_ring_t can_ack_ring;
static struct rt_semaphore can_activity_sem;

static uint8_t can_cur_mode[CAN_CH_NUM] = {CAN_MODE_SLEEP, CAN_MODE_SLEEP};
static uint32_t can_cur_kbps[CAN_CH_NUM] = {CAN_BITRATE_KBPS, CAN_BITRATE_KBPS};

#define CAN_PCLK1_HZ   72000000u

/* TXMIR位定义(RM 24.7.11, 库头文件只有按邮箱命名的宏, 运行时索引不适用) */
#define CAN_TXMIR_TXRQ   (1u << 0)
#define CAN_TXMIR_RTR    (1u << 1)
#define CAN_TXMIR_IDE    (1u << 2)
#define CAN_TQ_PER_BIT 18u    /* SJW1+BS1_13+BS2_4, 采样点77.8% */
#define MBOX_HANDLE_NONE 0xFFFFu

/* ---------------- 1µs时间戳(TIM3自由运行+回绕影子) ---------------- */
static volatile uint32_t can_ts_hi;

void TIM3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM3_IRQHandler(void)
{
    if (TIM3->INTFR & TIM_FLAG_Update) {
        TIM3->INTFR = ~TIM_FLAG_Update;
        can_ts_hi += 0x10000u;
    }
}

uint32_t bsp_can_now_us(void)
{
    uint32_t hi = can_ts_hi;
    uint16_t cnt = (uint16_t)TIM3->CNT;
    if (TIM3->CNT < cnt) {
        hi = can_ts_hi;   /* 读CNT期间回绕: 用新影子 */
    }
    return (hi & 0xFFFF0000u) | cnt;
}

static void can_timestamp_init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    TIM_TimeBaseInitTypeDef t = {0};
    TIM_TimeBaseStructInit(&t);
    t.TIM_Prescaler = CAN_PCLK1_HZ / 1000000u - 1;   /* 1MHz计数 */
    t.TIM_Period = 0xFFFF;
    t.TIM_ClockDivision = TIM_CKD_DIV1;
    t.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &t);
    TIM_ClearFlag(TIM3, TIM_FLAG_Update);
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);

    NVIC_InitTypeDef n = {0};
    n.NVIC_IRQChannel = TIM3_IRQn;
    n.NVIC_IRQChannelPreemptionPriority = 2;
    n.NVIC_IRQChannelSubPriority = 1;
    n.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&n);
    TIM_Cmd(TIM3, ENABLE);
}

/* ---------------- 环操作 ---------------- */

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

static uint8_t ack_ring_push_isr(can_ack_t *a)
{
    uint8_t nx = (can_ack_ring.head + 1) % CAN_ACK_RING_LEN;
    if (nx == can_ack_ring.tail) {
        g_dbg.can_ack_drop++;
        return 0;
    }
    can_ack_ring.buf[can_ack_ring.head] = *a;
    can_ack_ring.head = nx;
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

/* 线程侧回执入环(桥接层用: 发送环满时补DROPPED回执) */
void bsp_can_ack_push(can_ack_t *a)
{
    rt_base_t level = rt_hw_interrupt_disable();
    ack_ring_push_isr(a);
    rt_hw_interrupt_enable(level);
}

uint8_t bsp_can_ack_pop(can_ack_t *a)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (can_ack_ring.head == can_ack_ring.tail) {
        rt_hw_interrupt_enable(level);
        return 0;
    }
    *a = can_ack_ring.buf[can_ack_ring.tail];
    can_ack_ring.tail = (can_ack_ring.tail + 1) % CAN_ACK_RING_LEN;
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

struct rt_semaphore *bsp_can_activity_sem(void)
{
    return &can_activity_sem;
}

/* ---------------- TX路径 ---------------- */

/* 把帧装入一个空闲邮箱(直接寄存器写, ISR/线程均可调; 无空闲邮箱返回0) */
static uint8_t can_load_mailbox(uint8_t ch, const can_frame_t *f)
{
    can_chan_t *c = &can_chan[ch];
    CAN_TypeDef *CANx = c->reg;
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

    if (f->flags & CANF_IDE) {
        CANx->sTxMailBox[mbox].TXMIR = ((f->id & 0x1FFFFFFFu) << 3) | CAN_TXMIR_IDE;
    } else {
        CANx->sTxMailBox[mbox].TXMIR = ((f->id & 0x7FFu) << 21);
    }
    if (f->flags & CANF_RTR) {
        CANx->sTxMailBox[mbox].TXMIR |= CAN_TXMIR_RTR;
    }
    CANx->sTxMailBox[mbox].TXMDTR = f->dlc & 0x0F;
    memcpy(&wr, &f->data[0], 4);
    CANx->sTxMailBox[mbox].TXMDLR = wr;
    memcpy(&wr, &f->data[4], 4);
    CANx->sTxMailBox[mbox].TXMDHR = wr;
    c->mbox_handle[mbox] = f->handle;
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
        if (!can_load_mailbox(ch, &f)) {
            /* 邮箱满: 该帧放回环头 */
            uint8_t t = (uint8_t)((c->tx.tail + CAN_TX_RING_LEN - 1) % CAN_TX_RING_LEN);
            c->tx.buf[t] = f;
            c->tx.tail = t;
            break;
        }
        if (ch == CAN_CH1) g_dbg.can1_tx++; else g_dbg.can2_tx++;
    }
}

/* TX空中断: 先收割完成邮箱的RQCP产生回执, 再续装 */
static void can_tx_isr(uint8_t ch)
{
    can_chan_t *c = &can_chan[ch];
    CAN_TypeDef *CANx = c->reg;

    for (uint8_t m = 0; m < 3; m++) {
        uint32_t rqcp = (m == 0) ? CAN_TSTATR_RQCP0 : ((m == 1) ? CAN_TSTATR_RQCP1 : CAN_TSTATR_RQCP2);
        if (CANx->TSTATR & rqcp) {
            uint32_t txok = (m == 0) ? CAN_TSTATR_TXOK0 : ((m == 1) ? CAN_TSTATR_TXOK1 : CAN_TSTATR_TXOK2);
            uint32_t alst = (m == 0) ? CAN_TSTATR_ALST0 : ((m == 1) ? CAN_TSTATR_ALST1 : CAN_TSTATR_ALST2);
            can_ack_t a;
            a.handle = c->mbox_handle[m];
            a.ch = ch;
            if (c->mbox_handle[m] != MBOX_HANDLE_NONE) {
                a.status = (CANx->TSTATR & txok) ? CAN_ACK_SENT :
                           ((CANx->TSTATR & alst) ? CAN_ACK_ARB_LOST : CAN_ACK_ERROR);
                ack_ring_push_isr(&a);
                rt_sem_release(&can_activity_sem);
            }
            c->mbox_handle[m] = MBOX_HANDLE_NONE;
            CANx->TSTATR = rqcp;   /* 写1清除完成标志 */
        }
    }

    CAN_ClearITPendingBit(CANx, CAN_IT_TME);
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
        f.flags = (msg.IDE == CAN_Id_Extended) ? CANF_IDE : 0;
        if (msg.RTR == CAN_RTR_Remote) f.flags |= CANF_RTR;
        f.dlc = msg.DLC & 0x0F;
        f.id = (f.flags & CANF_IDE) ? msg.ExtId : msg.StdId;
        f.handle = 0;
        f.ts_us = bsp_can_now_us();
        memcpy(f.data, msg.Data, 8);
        if (ch == CAN_CH1) g_dbg.can1_rx++; else g_dbg.can2_rx++;
        if (!rx_ring_push_isr(&c->rx, &f)) {
            g_dbg.can_rx_drop++;
        }
    }
    CAN_ClearITPendingBit(c->reg, CAN_IT_FMP0);
    rt_sem_release(&can_activity_sem);
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

/* ---------------- 中断入口 ---------------- */
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

/* ---------------- 运行时重配置 ---------------- */

/* 放弃某通道所有在途发送: 环内帧+邮箱帧, 全部回执ABORT */
static void can_abort_pending(uint8_t ch)
{
    can_chan_t *c = &can_chan[ch];
    can_frame_t f;
    can_ack_t a;
    a.ch = ch;
    a.status = CAN_ACK_ABORT;
    while (tx_ring_pop_isr(&c->tx, &f)) {
        a.handle = f.handle;
        ack_ring_push_isr(&a);
    }
    for (uint8_t m = 0; m < 3; m++) {
        if (c->mbox_handle[m] != MBOX_HANDLE_NONE) {
            a.handle = c->mbox_handle[m];
            ack_ring_push_isr(&a);
            c->mbox_handle[m] = MBOX_HANDLE_NONE;
        }
    }
}

/* 进入初始化模式重写BTIMR(模式+波特率)后退出; SLEEP模式则停在睡眠 */
static int can_hw_reconfig(uint8_t ch, uint32_t kbps, uint8_t mode)
{
    CAN_TypeDef *CANx = can_chan[ch].reg;
    uint32_t tout;

    uint32_t bitrate = kbps * 1000u;
    uint32_t brp = CAN_PCLK1_HZ / (bitrate * CAN_TQ_PER_BIT);
    if (brp < 1 || brp > 1024 || (CAN_PCLK1_HZ % (bitrate * CAN_TQ_PER_BIT)) != 0) {
        return -1;   /* 无法整除出18tq时序 */
    }

    can_abort_pending(ch);

    uint32_t level = rt_hw_interrupt_disable();   // 关CAN中断防重入
    CANx->INTENR = 0;

    CANx->CTLR &= ~CAN_CTLR_SLEEP;
    CANx->CTLR |= CAN_CTLR_INRQ;
    tout = 0;
    while (!(CANx->STATR & CAN_STATR_INAK) && ++tout < 1000000);
    if (!(CANx->STATR & CAN_STATR_INAK)) {
        rt_hw_interrupt_enable(level);
        return -2;
    }

    /* BTIMR: BRP[9:0] SJW[11:10] TS2[15:12] TS1[19:16] LBKM[30] SILM[31] */
    uint32_t bt = (brp - 1) | (0u << 10) | (3u << 12) | (12u << 16);
    if (mode == CAN_MODE_LOOPBACK_TEST) {
        bt |= (3u << 30);   /* SILM+LBKM */
    }
    CANx->BTIMR = bt;

    if (mode == CAN_MODE_SLEEP) {
        CANx->CTLR |= CAN_CTLR_SLEEP;
    }
    CANx->CTLR &= ~CAN_CTLR_INRQ;
    tout = 0;
    while ((CANx->STATR & CAN_STATR_INAK) && ++tout < 1000000);

    CAN_ClearITPendingBit(CANx, CAN_IT_TME);
    CANx->RFIFO0 = CAN_RFIFO0_RFOM0;   /* 丢弃模式切换期间的陈旧接收 */
    CANx->INTENR = CAN_IT_FMP0 | CAN_IT_TME;
    rt_hw_interrupt_enable(level);

    can_cur_mode[ch] = mode;
    can_cur_kbps[ch] = kbps;
    rt_sem_release(&can_activity_sem);
    return 0;
}

int bsp_can_set_bitrate(uint8_t ch, uint32_t kbps)
{
    if (ch >= CAN_CH_NUM) return -1;
    if (kbps == 0 || kbps > 1000) return -1;
    return can_hw_reconfig(ch, kbps, can_cur_mode[ch]);
}

int bsp_can_set_mode(uint8_t ch, uint8_t mode)
{
    if (ch >= CAN_CH_NUM) return -1;
    if (mode > CAN_MODE_SLEEP) return -1;
    return can_hw_reconfig(ch, can_cur_kbps[ch], mode);
}

uint32_t bsp_can_get_bitrate(uint8_t ch)
{
    return (ch < CAN_CH_NUM) ? can_cur_kbps[ch] : 0;
}

uint8_t bsp_can_get_mode(uint8_t ch)
{
    return (ch < CAN_CH_NUM) ? can_cur_mode[ch] : CAN_MODE_SLEEP;
}

/* ---------------- 初始化 ---------------- */

int bsp_can_init(uint8_t mode)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    CAN_InitTypeDef CAN_InitStructure = {0};
    CAN_FilterInitTypeDef CAN_FilterInitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    uint32_t bitrate = CAN_BITRATE_KBPS * 1000u;
    uint32_t brp = CAN_PCLK1_HZ / (bitrate * CAN_TQ_PER_BIT);
    if (brp < 1 || brp > 1024 || (CAN_PCLK1_HZ % (bitrate * CAN_TQ_PER_BIT)) != 0) {
        return -1;
    }

    rt_sem_init(&can_activity_sem, "can_act", 0, RT_IPC_FLAG_FIFO);
    can_timestamp_init();

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
    for (uint8_t ch = 0; ch < CAN_CH_NUM; ch++) {
        for (uint8_t m = 0; m < 3; m++) {
            can_chan[ch].mbox_handle[m] = MBOX_HANDLE_NONE;
        }
        can_cur_mode[ch] = mode;
        can_cur_kbps[ch] = CAN_BITRATE_KBPS;
    }

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

    NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
    NVIC_Init(&NVIC_InitStructure);

    CAN_ITConfig(CAN1, CAN_IT_FMP0 | CAN_IT_TME, ENABLE);
    CAN_ITConfig(CAN2, CAN_IT_FMP0 | CAN_IT_TME, ENABLE);
    return 0;
}

static int bsp_can_board_init(void)
{
    return bsp_can_init(CAN_MODE_SLEEP);
}
INIT_BOARD_EXPORT(bsp_can_board_init);
