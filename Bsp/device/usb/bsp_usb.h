#ifndef __BSP_USB_H
#define __BSP_USB_H

#include "stdint.h"
#include "rtthread.h"
#include "usbd_core.h"
#include "bsp_pool.h"

#ifdef __cplusplus
 extern "C" {
#endif

/* 本层只做USB设备层: 控制器初始化/描述符表(复合布局=设备级配置)/接口端点
 * 有序注册/CONFIGURED等事件分发。各功能的数据面(CDC桥/CAN桥/DAP命令泵)
 * 在各自App模块内实现, 通过 INIT_COMPONENT 阶段填充句柄的 ep_addr/ep_cb/
 * cfg_cb 完成向下注册(早于本层 INIT_ENV 的 usbd_initialize), 本层零向上
 * extern -- 2026-10-01解耦重构, 曾数据面混在bsp_usb.c且extern App回调 */

typedef void (*bsp_usb_cfg_hook_t)(uint8_t busid, uint8_t event);

/* CDC缓存管理: OUT(usb->uart)走块缓冲池(USB落块零拷贝交接UART DMA) */
typedef struct {
    bsp_pool_t rx_out_pool;                          // CDC OUT块缓冲池
    rt_bool_t tx_buffer_empty;                       // USB IN方向无在途发送
    rt_bool_t allow_transfer;                        // 允许数据传输(线控已配置)
    volatile uint8_t led_blink_req;                  // LED确认双闪请求(monitor线程消费)
    struct rt_semaphore data_ready_sem;              // CDC OUT数据就绪信号量(块已提交)
    struct rt_semaphore lincoding_sem;               // 设备配置信号量
    struct usbd_endpoint in_ep;
    struct usbd_endpoint out_ep;
    bsp_usb_cfg_hook_t cdc_event_hander_callback;    // 事件钩子(CONFIGURED重建等)
} bsp_usb_cdc_buffer_t;

typedef struct {
    struct rt_semaphore request_sem;              // 数据就绪信号量
    struct usbd_endpoint in_ep;
    struct usbd_endpoint out_ep;
    bsp_usb_cfg_hook_t cfg_cb;                    // 事件钩子(ep_open+挂首包)
} bsp_usb_dap_buffer_t;

/* CAN桥USB接口: OUT(usb->can)块缓冲池 + IN(can->usb)拼装块池
 * 协议与数据面在App/can_bridge_thread.c, 本结构仅承载注册状态 */
typedef struct {
    bsp_pool_t out_pool;                          // CAN OUT块缓冲池
    struct usbd_endpoint in_ep;
    struct usbd_endpoint out_ep;
    bsp_usb_cfg_hook_t cfg_cb;                    // 事件钩子(CONFIGURED时ep_open+重挂)
} bsp_usb_can_buffer_t;

extern bsp_usb_can_buffer_t canusb_handle;
extern bsp_usb_cdc_buffer_t cdc_handle;
extern bsp_usb_dap_buffer_t dap_handle;

#ifdef __cplusplus
}
#endif

#endif /* __BSP_USB_H */
