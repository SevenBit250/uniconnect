#include <rtthread.h>
#include <string.h>
#include "config.h"
#include "bsp_can.h"
#include "bsp_led.h"
#include "bsp_pool.h"

/* CAN双路静默回环自测线程(驱动起来阶段的验收):
 * 每通道独立发序列帧 -> 硬件回环 -> RX中断收帧 -> 此线程校验
 * 帧内容: id=0x100+ch, data[0..3]=seq(LE), data[4..7]=~seq(LE), dlc=8
 * seq跳变计canX_err(丢帧), 内容错计canX_err; LED每512帧翻转示活
 * 后续接USB桥时此线程整体替换 */

static struct rt_thread can_test_thread;
ALIGN(RT_ALIGN_SIZE) static char can_test_stack[CAN_TEST_THREAD_STACK_SIZE];

static uint8_t can_test_one_ch(uint8_t ch)
{
    static uint32_t seq[CAN_CH_NUM];
    can_frame_t f;
    uint32_t s;
    uint8_t any = 0;

    /* 发一帧(环有余量才发, 避免灌满挤掉在途帧) */
    if (bsp_can_tx_free(ch) > 4) {
        f.id = 0x100 + ch;
        f.ide = 0;
        f.rtr = 0;
        f.dlc = 8;
        s = seq[ch];
        f.data[0] = (uint8_t)s;
        f.data[1] = (uint8_t)(s >> 8);
        f.data[2] = (uint8_t)(s >> 16);
        f.data[3] = (uint8_t)(s >> 24);
        f.data[4] = (uint8_t)~s;
        f.data[5] = (uint8_t)~(s >> 8);
        f.data[6] = (uint8_t)~(s >> 16);
        f.data[7] = (uint8_t)~(s >> 24);
        seq[ch] = s + 1;
        bsp_can_send(ch, &f);
    }

    /* 收并校验(一次最多处理8帧, 把机会留给另一通道) */
    for (int i = 0; i < 8; i++) {
        can_frame_t r;
        if (!bsp_can_rx_pop(ch, &r)) {
            break;
        }
        any = 1;
        uint32_t *errp = (ch == CAN_CH1) ? &g_dbg.can1_err : &g_dbg.can2_err;
        uint32_t expect = (uint32_t)(r.data[0] | (r.data[1] << 8) |
                            (r.data[2] << 16) | ((uint32_t)r.data[3] << 24));
        uint32_t inv = (uint32_t)(r.data[4] | (r.data[5] << 8) |
                          (r.data[6] << 16) | ((uint32_t)r.data[7] << 24));
        static uint32_t last_seq[CAN_CH_NUM];
        if (r.id != (uint32_t)(0x100 + ch) || r.dlc != 8 ||
            inv != ~expect || expect != last_seq[ch]) {
            (*errp)++;   /* id/dlc/内容错 或 seq跳变(丢帧) */
        }
        last_seq[ch] = expect + 1;
    }
    return any;
}

static void can_test_thread_entry(void *param)
{
    (void)param;
    uint32_t tick = 0;
    /* 上电稍等USB/系统稳定 */
    rt_thread_mdelay(200);
    while (1) {
        uint8_t a1 = can_test_one_ch(CAN_CH1);
        uint8_t a2 = can_test_one_ch(CAN_CH2);
        tick++;
        if ((tick & 0x1FF) == 0) {   /* 每512轮翻转LED示活 */
            if (a1) bsp_led_toggle(BSP_LED_ID_CAN1_STA);
            if (a2) bsp_led_toggle(BSP_LED_ID_CAN2_STA);
        }
        /* 节流: 自测期~3000轮/s量级, 留CPU给USB */
        rt_thread_mdelay(1);
    }
}

int can_test_thread_init(void)
{
    rt_thread_init(&can_test_thread,
                   "can_test",
                   can_test_thread_entry, RT_NULL,
                   can_test_stack, sizeof(can_test_stack),
                   CAN_TEST_THREAD_PRIORITY, 100);
    rt_thread_startup(&can_test_thread);
    return 0;
}
INIT_APP_EXPORT(can_test_thread_init);
