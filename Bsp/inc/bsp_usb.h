#ifndef __BSP_USB_H
#define __BSP_USB_H

#include "rtthread.h"
#include "usbd_core.h"
#include "chry_ringbuffer.h"

#ifdef __cplusplus
 extern "C" {
#endif

typedef struct {
    chry_ringbuffer_t rb_cdc_rx;                     // 缓冲管理器
    rt_bool_t rx_buffer_full;                        // 接收缓冲满
    rt_bool_t tx_buffer_empty;                       // 发送缓冲空
    rt_bool_t allow_transfer;                        // 允许数据传输
    rt_bool_t uart_tx_working;
    rt_bool_t uart_rx_working;
    struct rt_semaphore data_ready_sem;              // 数据就绪信号量
    struct rt_semaphore lincoding_sem;               // 设备配置信号量
    struct usbd_endpoint in_ep;
    struct usbd_endpoint out_ep;
    void(*cdc_event_hander_callback)(uint8_t busid, uint8_t event);
} bsp_usb_cdc_buffer_t;

typedef struct {
    struct rt_semaphore request_sem;              // 数据就绪信号量
    struct usbd_endpoint in_ep;
    struct usbd_endpoint out_ep;
    void(*dap_event_hander_callback)(uint8_t busid, uint8_t event);
} bsp_usb_dap_buffer_t;

int bsp_usb_cdc_ep_start_read(uint8_t busid);

int bsp_usb_cdc_ep_start_write(uint8_t busid, const uint8_t *data, uint32_t data_len);

uint8_t* bsp_usb_cdc_get_buffer(void);

int bsp_usb_cdc_get_buffer_size(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_USB_H */


