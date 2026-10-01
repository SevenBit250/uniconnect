#ifndef __BSP_WATCHDOG_H
#define __BSP_WATCHDOG_H

#include <stdint.h>

/* 周期校验金丝雀与USBHS OUT DMA指向, 命中即记录崩溃盒并死循环 */
void bsp_watchdog_check(void);

#endif
