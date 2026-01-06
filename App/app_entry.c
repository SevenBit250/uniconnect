#include "main.h"
#include "ch32v30x.h"
#include <rtthread.h>


ALIGN(RT_ALIGN_SIZE)
static char swd_thread_stack[1024];
static struct rt_thread swd_thread;
static void swd_thread_entry(void *param);

static int app_entry(void)
{
    /* 初始化swd线程 */
    rt_thread_init(&swd_thread,
                   "swd_thread",
                   swd_thread_entry,
                   RT_NULL,
                   &swd_thread_stack[0],
                   sizeof(swd_thread_stack),
                   20, 
                   rt_tick_from_millisecond(1));
    rt_thread_startup(&swd_thread);

    return 0;
}
INIT_APP_EXPORT(app_entry);

/* 线程2入口 */
static void swd_thread_entry(void *param)
{
    while (1) {
        rt_thread_mdelay(1000);
        // GPIO_WriteBit(SYS_STA_Port, SYS_STA_Pin, !GPIO_ReadOutputDataBit(SYS_STA_Port, SYS_STA_Pin));
    }
}