/*
 * Copyright (c) 2022, sakumisu
 * Copyright (c) 2022-2025, HPMicro
 * Copyright (c) 2025-07, SevenBit
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef CHERRYUSB_CONFIG_H
#define CHERRYUSB_CONFIG_H

/* ================ USB common Configuration ================ */

#ifdef __RTTHREAD__
#include <rtthread.h>

#define CONFIG_USB_PRINTF(...) rt_kprintf(__VA_ARGS__)
#else
#define CONFIG_USB_PRINTF(...) printf(__VA_ARGS__)
#endif

#ifndef CONFIG_USB_DBG_LEVEL
#define CONFIG_USB_DBG_LEVEL USB_DBG_INFO
#endif

#if defined(CONFIG_USB_DEVICE_FS) || defined(CONFIG_USB_DEVICE_FORCE_FULL_SPEED)
#undef CONFIG_USB_HS
#else
#define CONFIG_USB_HS
#endif

/* Enable print with color */
// #define CONFIG_USB_PRINTF_COLOR_ENABLE

/* #define CONFIG_USB_DCACHE_ENABLE */

/* data align size when use dma or use dcache */
#ifndef CONFIG_USB_ALIGN_SIZE
#define CONFIG_USB_ALIGN_SIZE 4
#endif

/* attribute data into no cache ram */
#define USB_NOCACHE_RAM_SECTION __attribute__((section(".noncacheable")))

/* use usb_memcpy default for high performance but cost more flash memory.
 * And, arm libc has a bug that memcpy() may cause data misalignment when the size is not a multiple of 4.
*/
/* #define CONFIG_USB_MEMCPY_DISABLE */

/* ================= USB Device Stack Configuration ================ */

/* Ep0 in and out transfer buffer */
#ifndef CONFIG_USBDEV_REQUEST_BUFFER_LEN
#define CONFIG_USBDEV_REQUEST_BUFFER_LEN 512
#endif

/* Setup packet log for debug */
/* #define CONFIG_USBDEV_SETUP_LOG_PRINT */

/* Send ep0 in data from user buffer instead of copying into ep0 reqdata
 * Please note that user buffer must be aligned with CONFIG_USB_ALIGN_SIZE
 */
/* #define CONFIG_USBDEV_EP0_INDATA_NO_COPY */

/* Check if the input descriptor is correct */
/* #define CONFIG_USBDEV_DESC_CHECK */

/* Enable test mode */
#define CONFIG_USBDEV_TEST_MODE

/* enable advance desc register api */
#define CONFIG_USBDEV_ADVANCE_DESC

/* move ep0 setup handler from isr to thread */
/* #define CONFIG_USBDEV_EP0_THREAD */

#ifndef CONFIG_USBDEV_EP0_PRIO
#define CONFIG_USBDEV_EP0_PRIO 4
#endif

#ifndef CONFIG_USBDEV_EP0_STACKSIZE
#define CONFIG_USBDEV_EP0_STACKSIZE 2048
#endif

/* ================ USB Device Port Configuration ================*/

#define CONFIG_USBDEV_MAX_BUS 1

#ifndef CONFIG_USBDEV_EP_NUM
#define CONFIG_USBDEV_EP_NUM 16
#endif

#endif
