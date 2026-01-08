#include "bsp.h"
#include "main.h"
#include <rtthread.h>

static struct rt_thread monitor_thread;
ALIGN(RT_ALIGN_SIZE) static char monitor_thread_stack[1024];


static void monitor_thread_entry(void *param)
{
    static system_status_t last_state;
    while (1) {
        system_status_t state = getSystemStatus();
        switch (state) {
            case SYS_STA_USB_NOT_CONNECTED:
                for (uint8_t i = 0b10000, ii = 0b00001; i > 0; i >>= 1, ii <<= 1) {
                    bsp_led_ctrl_bit(i | ii);
                    rt_thread_mdelay(50);
                }
            break;

            case SYS_STA_USB_CONNECTED:
                if (last_state != state && last_state == SYS_STA_USB_NOT_CONNECTED) {
                    bsp_led_ctrl_bit(0b00000);
                }
                bsp_led_ctrl(BSP_LED_ID_SYS_STA, 1);
                rt_thread_mdelay(1000);
                bsp_led_ctrl(BSP_LED_ID_SYS_STA, 0);
                rt_thread_mdelay(1000);
            break;

            default:
            break;
        }
        last_state = state;
    }
}

static int monitor_thread_init(void)
{
    /* 初始化monitor线程 */
    rt_thread_init(&monitor_thread,
                   "monitor",
                   monitor_thread_entry,
                   RT_NULL,
                   &monitor_thread_stack[0],
                   sizeof(monitor_thread_stack),
                   20, 
                   rt_tick_from_millisecond(1));

    rt_thread_startup(&monitor_thread);
    return 0;
}
INIT_APP_EXPORT(monitor_thread_init);

