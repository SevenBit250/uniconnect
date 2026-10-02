/* bsp_led.c -- STA LED + SysTick 1ms 时基(boot 专属 bsp)
 * SysTick 配置走 board.c 同款 WCH 序列(CTLR/CMP, 非ARM式LOAD)。 */
#include "ch32v30x.h"
#include "pin.h"
#include "bsp_led.h"

static volatile uint32_t s_ms;

/* SysTick中断: 先清SR(WCH的COUNTFO必须软件清, 不清则mret后立刻重触发
 * =中断风暴饿死主循环, APP侧board.c同款写法, 2026-10-03 boot实锚) */
void SysTick_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void SysTick_Handler(void)
{
    SysTick->SR = 0;
    s_ms++;
}

void boot_led_init(void)
{
    GPIO_InitTypeDef g = {0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_SetBits(PIN_LED_STA_PORT, PIN_LED_STA_PIN);   /* 高=灭 */
    g.GPIO_Pin = PIN_LED_STA_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PIN_LED_STA_PORT, &g);

    /* SysTick 1ms @ boot主频96MHz(system分叉, 见boot/User/system_ch32v30x.c) */
    NVIC_SetPriority(SysTicK_IRQn, 0xF0);
    NVIC_EnableIRQ(SysTicK_IRQn);
    SysTick->CTLR = 0;
    SysTick->SR = 0;
    SysTick->CNT = 0;
    SysTick->CMP = (SystemCoreClock / 1000u) - 1u;
    SysTick->CTLR = 0xF;                 /* STE|STIE|STCLK(hclk)|STRE */
}

void boot_led_sta_toggle(void)
{
    GPIO_WriteBit(PIN_LED_STA_PORT, PIN_LED_STA_PIN,
                  (BitAction)!GPIO_ReadOutputDataBit(PIN_LED_STA_PORT, PIN_LED_STA_PIN));
}

uint32_t boot_ms(void)
{
    return s_ms;
}

void boot_delay_ms(uint32_t ms)
{
    uint32_t t0 = s_ms;
    while (s_ms - t0 < ms) { }
}

/* 忙等变体: 供MIE=0的上下文(jump_to_app全程关全局中断, SysTick中断
 * 不达 -> boot_delay_ms会死等, 2026-10-03实锚)。~8cyc/迭代@96MHz */
void boot_delay_ms_blocking(uint32_t ms)
{
    while (ms--) {
        for (volatile uint32_t i = 0; i < 12000; i++) {
            __asm volatile("nop");
        }
    }
}

void boot_systick_stop(void)
{
    SysTick->CTLR = 0;
    SysTick->SR = 0;
}
