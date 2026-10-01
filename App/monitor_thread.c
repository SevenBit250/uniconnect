#include "bsp.h"
#include "main.h"
#include "config.h"
#include <rtthread.h>
#include "bsp_watchdog.h"
#include "bsp_pool.h"
#include "bsp_usb.h"

/* 统一LED管理线程(兼看门狗): 全部5灯集中于此, 数据路径零LED代码。
 * - SYS灯: 未连接=对称流水(50ms步), 已连接=1s心跳
 * - COM/CAN1/CAN2灯: 吞吐活动灯 -- 10ms轮询g_dbg计数器, 有增量则亮
 *   LED_MIN_ON_TICKS(30ms, 人眼可靠感知下限), 无活动灭; 块密集时自然常亮,
 *   稀疏时每块一次可见闪光, 闪烁频率随吞吐连续变化
 * - COM灯另承载线控配置确认双闪(cdc_handle.led_blink_req)
 * 曾为独立cdc_sta_led线程(1ms轮询布尔标志, 高速toggle人眼只见常亮偏暗) */
static struct rt_thread monitor_thread;
ALIGN(RT_ALIGN_SIZE) static char monitor_thread_stack[MONITOR_THREAD_STACK_SIZE];

#define LED_MIN_ON_TICKS 3u   /* 活动灯最小亮时长, 10ms/tick */

static void monitor_thread_entry(void *param)
{
    static system_status_t last_state;
    uint32_t tick = 0;
    uint32_t anim_step = 0;
    uint32_t last_com = 0, last_c1 = 0, last_c2 = 0;
    uint8_t com_on = 0, c1_on = 0, c2_on = 0;
    (void)param;

    while (1) {
        rt_thread_mdelay(10);
        tick++;

        bsp_watchdog_check();   /* 金丝雀+OUT DMA指向看门狗 */

        system_status_t state = getSystemStatus();
        switch (state) {
            case SYS_STA_USB_NOT_CONNECTED:
                /* 对称流水(bit4|bit0 -> bit2 -> bit0|bit4), 50ms步进 */
                if (tick % 5 == 0) {
                    bsp_led_ctrl_bit((0b10000 >> anim_step) | (0b00001 << anim_step));
                    anim_step = (anim_step + 1) % 5;
                }
            break;

            case SYS_STA_USB_CONNECTED:
                if (last_state != state) {
                    bsp_led_ctrl_bit(0b00000);
                    /* 计数器基线重取, 防跨会话增量误亮 */
                    last_com = g_dbg.uart_submits + g_dbg.tx_dma;
                    last_c1 = g_dbg.can1_tx + g_dbg.can1_rx;
                    last_c2 = g_dbg.can2_tx + g_dbg.can2_rx;
                }
                /* SYS心跳 */
                if (tick % 100 == 0) {
                    bsp_led_toggle(BSP_LED_ID_SYS_STA);
                }
                /* 线控配置确认: 双闪(阻塞120ms无害, 看门狗10ms粒度) */
                if (cdc_handle.led_blink_req) {
                    cdc_handle.led_blink_req = 0;
                    for (uint8_t i = 0; i < 2; i++) {
                        bsp_led_ctrl(BSP_LED_ID_COM_STA, 1);
                        rt_thread_mdelay(30);
                        bsp_led_ctrl(BSP_LED_ID_COM_STA, 0);
                        rt_thread_mdelay(30);
                    }
                    break;
                }
                /* 吞吐活动灯 */
                if (g_dbg.uart_submits + g_dbg.tx_dma != last_com) {
                    last_com = g_dbg.uart_submits + g_dbg.tx_dma;
                    com_on = LED_MIN_ON_TICKS;
                }
                if (g_dbg.can1_tx + g_dbg.can1_rx != last_c1) {
                    last_c1 = g_dbg.can1_tx + g_dbg.can1_rx;
                    c1_on = LED_MIN_ON_TICKS;
                }
                if (g_dbg.can2_tx + g_dbg.can2_rx != last_c2) {
                    last_c2 = g_dbg.can2_tx + g_dbg.can2_rx;
                    c2_on = LED_MIN_ON_TICKS;
                }
                bsp_led_ctrl(BSP_LED_ID_COM_STA, com_on != 0);
                bsp_led_ctrl(BSP_LED_ID_CAN1_STA, c1_on != 0);
                bsp_led_ctrl(BSP_LED_ID_CAN2_STA, c2_on != 0);
                if (com_on) com_on--;
                if (c1_on) c1_on--;
                if (c2_on) c2_on--;
            break;

            default:
            break;
        }
        last_state = state;
    }
}

static int monitor_thread_init(void)
{
    /* 初始化monitor线程(优先级20: LED/看门狗皆缓务, 低于一切数据通路) */
    rt_thread_init(&monitor_thread,
                   "monitor",
                   monitor_thread_entry,
                   RT_NULL,
                   &monitor_thread_stack[0],
                   sizeof(monitor_thread_stack),
                   MONITOR_THREAD_PRIORITY,
                   rt_tick_from_millisecond(1));

    rt_thread_startup(&monitor_thread);
    return 0;
}
INIT_APP_EXPORT(monitor_thread_init);
