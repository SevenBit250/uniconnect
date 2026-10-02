/* bsp_led.h -- boot 专属最小 bsp: STA LED + SysTick 1ms 时基
 *
 * 引脚来自 common/pin.h(全板单一事实源); boot 不复用 app/Bsp(那边依赖
 * RT-Thread 线程模型, boot 裸机只要一盏灯和一个准的毫秒)。
 * APP 侧接管 SysTick 前须 boot_systick_stop()(jump_to_app 已做)。 */
#ifndef __BOOT_BSP_LED_H
#define __BOOT_BSP_LED_H

#include <stdint.h>

void     boot_led_init(void);          /* STA灯GPIO(初始灭) + SysTick 1ms 起振 */
void     boot_led_sta_toggle(void);
uint32_t boot_ms(void);                /* 毫秒计数(单调, 约49天回绕) */
void     boot_delay_ms(uint32_t ms);   /* 基于boot_ms(需全局中断开启, 升级态用) */
void     boot_delay_ms_blocking(uint32_t ms); /* 忙等(MIE=0上下文可用, 跳转路径用) */
void     boot_systick_stop(void);      /* 跳APP前关SysTick(防中断穿越跳转窗口) */

#endif /* __BOOT_BSP_LED_H */
