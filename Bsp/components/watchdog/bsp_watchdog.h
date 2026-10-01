#ifndef __BSP_WATCHDOG_H
#define __BSP_WATCHDOG_H

#include <stdint.h>

/* 周期校验金丝雀与USBHS OUT DMA指向, 命中即记录崩溃盒并死循环 */

/* 登记一个受监护的OUT端点: 周期校验其RX_DMA指向落在[pool_base, pool_base+pool_size)
 * (注册制, 数据面模块init时调用; 曾extern cdc_handle直达, 2026-10-01解耦)。
 * ep_num为端点号(1..15), 传入0取消监护 */
void bsp_watchdog_guard_out_ep(uint8_t ep_num, uint32_t pool_base, uint32_t pool_size);

void bsp_watchdog_check(void);

#endif
