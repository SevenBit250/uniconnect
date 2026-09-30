#ifndef __BSP_USB_H
#define __BSP_USB_H

#include "stdint.h"
#include "rtthread.h"
#include "usbd_core.h"
#include "bsp_pool.h"

#ifdef __cplusplus
 extern "C" {
#endif

/* CDC缓存管理: OUT(usb->uart)走块缓冲池(USB落块零拷贝交接UART DMA) */
typedef struct {
    bsp_pool_t rx_out_pool;                          // CDC OUT块缓冲池
    rt_bool_t tx_buffer_empty;                       // USB IN方向无在途发送
    rt_bool_t allow_transfer;                        // 允许数据传输(线控已配置)
    rt_bool_t uart_tx_working;                       // UART TX工作中(LED指示)
    rt_bool_t uart_rx_working;                       // UART RX工作中(LED指示)
    struct rt_semaphore data_ready_sem;              // CDC OUT数据就绪信号量(块已提交)
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

/* CAN桥USB接口: OUT(usb->can)块缓冲池 + IN(can->usb)拼装块池
 * 协议逻辑在App/can_bridge_thread.c, 本层只做端点/池搬运(镜像CDC模式) */
typedef struct {
    bsp_pool_t out_pool;                          // CAN OUT块缓冲池
    struct rt_semaphore data_ready_sem;           // OUT块就绪
    struct usbd_endpoint in_ep;
    struct usbd_endpoint out_ep;
} bsp_usb_can_buffer_t;

extern bsp_usb_can_buffer_t canusb_handle;

int bsp_can_usb_ep_start_write(uint8_t busid, const uint8_t *data, uint32_t data_len);
int bsp_can_usb_get_mps(void);

/* OUT块消费接口(块首恒4字节对齐, 用完recycle) */
uint8_t bsp_can_usb_rx_pop(uint8_t **block, uint16_t *len);
void bsp_can_usb_rx_recycle(uint8_t *block);
void bsp_can_usb_rx_try_resume(void);
/* 重建OUT池并重挂端点(CONFIGURED/总线复位时调用) */
void bsp_can_usb_rx_reset(void);
/* 开启CAN桥端点(CONFIGURED事件) */
void bsp_can_usb_ep_open(void);

int bsp_usb_cdc_ep_start_write(uint8_t busid, const uint8_t *data, uint32_t data_len);

/* CDC bulk MPS(512), 供ZLP等判断 */
int bsp_usb_cdc_get_mps(void);

/* OUT块缓冲池消费接口: 块首恒4字节对齐, 用完必须recycle */
uint8_t bsp_usb_cdc_rx_pop(uint8_t **block, uint16_t *len);

void bsp_usb_cdc_rx_recycle(uint8_t *block);

/* 若OUT因池耗尽而NAK挂起, 尝试取块重新挂端点 (recycle之后调用) */
void bsp_usb_cdc_rx_try_resume(void);

/* 重建OUT池(线控切换/USB复位): uart_inflight为UART TX在途块或NULL, 保留待归还 */
void bsp_usb_cdc_rx_reset(uint8_t *uart_inflight);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_USB_H */
