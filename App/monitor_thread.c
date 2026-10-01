#include "bsp.h"
#include "main.h"
#include "config.h"
#include <rtthread.h>
#include "bsp_watchdog.h"
#include "bsp_pool.h"
#include "bsp_can.h"

/* 统一LED管理线程(兼看门狗): 5灯集中渲染, 数据路径只写标志/计数器。
 * - STA灯: USB未连接=1s周期闪烁, 已连接=常亮
 * - COM/DAP/CAN1/CAN2功能灯: 未open=灭, open无数据=常亮, 有数据=200ms固定间隔闪烁
 *   open源: COM=主机DTR(g_led_open.com), DAP=HostStatus(g_led_open.dap_conn),
 *           CAN1/2=bsp_can_get_mode(硬件通道SET_MODE驱动, 上电默认close)
 *   活动源: 计数器200ms窗口增量, 增量优先于open(不置DTR的主机传数据灯也闪)
 * 曾为: 5灯流水/1s心跳/30ms活动灯/线控双闪/独立cdc_sta_led线程, 已并入本模型 */
static struct rt_thread monitor_thread;
ALIGN(RT_ALIGN_SIZE) static char monitor_thread_stack[MONITOR_THREAD_STACK_SIZE];

/* dap_thread.c链路观测: [1]=OUT回调次数(作DAP灯活动计数) */
extern volatile uint32_t g_dap_dbg[4];

#define LED_BLINK_TICKS     20u   /* 功能灯闪烁半周期200ms(线程10ms步) */
#define LED_STA_BLINK_TICKS 50u   /* STA灯闪烁半周期500ms(1s周期) */

typedef struct {
    uint8_t led_id;
    uint32_t (*snap)(void);              /* 活动计数快照 */
    const volatile uint8_t *open_flag;   /* NULL=CAN通道: 按mode判open */
    uint8_t can_ch;
    uint32_t last;
    uint8_t phase;                       /* 闪烁相位(活动窗口内每周期翻转) */
} func_led_t;

static uint32_t led_snap_com(void) { return g_dbg.uart_submits + g_dbg.tx_dma; }
static uint32_t led_snap_dap(void) { return g_dap_dbg[1]; }
static uint32_t led_snap_c1(void)  { return g_dbg.can1_tx + g_dbg.can1_rx; }
static uint32_t led_snap_c2(void)  { return g_dbg.can2_tx + g_dbg.can2_rx; }

static func_led_t func_leds[] = {
    {BSP_LED_ID_COM_STA,  led_snap_com, &g_led_open.com,      0,       0, 0},
    {BSP_LED_ID_SWD_STA,  led_snap_dap, &g_led_open.dap_conn, 0,       0, 0},
    {BSP_LED_ID_CAN1_STA, led_snap_c1,  RT_NULL,              CAN_CH1, 0, 0},
    {BSP_LED_ID_CAN2_STA, led_snap_c2,  RT_NULL,              CAN_CH2, 0, 0},
};
#define FUNC_LED_NUM (sizeof(func_leds) / sizeof(func_leds[0]))

static void monitor_thread_entry(void *param)
{
    system_status_t last_state = (system_status_t)0xFF;   /* 初值必异, 强制首周期边沿 */
    uint32_t tick = 0;
    (void)param;

    while (1) {
        rt_thread_mdelay(10);
        tick++;

        bsp_watchdog_check();   /* 金丝雀+OUT DMA指向看门狗 */

        system_status_t state = getSystemStatus();
        if (state != last_state) {
            /* 边沿: STA立即定态, 功能灯活动基线重取(防跨会话增量假闪) */
            for (uint32_t i = 0; i < FUNC_LED_NUM; i++) {
                func_leds[i].last = func_leds[i].snap();
                func_leds[i].phase = 0;
            }
            if (state == SYS_STA_USB_CONNECTED) {
                bsp_led_ctrl(BSP_LED_ID_SYS_STA, 1);
            } else {
                bsp_led_ctrl_bit(0);   /* 未连接: 全灭, STA由下方500ms翻转起闪 */
            }
        }

        if (state == SYS_STA_USB_NOT_CONNECTED) {
            if (tick % LED_STA_BLINK_TICKS == 0) {
                bsp_led_toggle(BSP_LED_ID_SYS_STA);
            }
        } else {
            if (tick % LED_BLINK_TICKS == 0) {
                for (uint32_t i = 0; i < FUNC_LED_NUM; i++) {
                    func_led_t *l = &func_leds[i];
                    uint8_t open = (l->open_flag != RT_NULL)
                                 ? *l->open_flag
                                 : (bsp_can_get_mode(l->can_ch) != CAN_MODE_SLEEP);
                    uint32_t now = l->snap();
                    if (now != l->last) {
                        l->phase ^= 1u;
                        bsp_led_ctrl(l->led_id, l->phase);
                    } else {
                        bsp_led_ctrl(l->led_id, open);
                    }
                    l->last = now;
                }
            }
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
