/**
 * @file dap_thread.c
 * @author awei (heqiweii@163.com)
 * @brief dap交互
 * @version 0.1
 * @date 2026-01-09
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#include "config.h"
#include "usbd_core.h"
#include "rtthread.h"
#include "DAP_config.h"
#include "DAP.h"
#include "bsp.h"
#include "bsp_config.h"

extern bsp_usb_dap_buffer_t dap_handle;

/* DAP链路观测计数器(openocd mdw读取): [0]CONFIGURED次数 [1]OUT回调次数
 * [2]最近一次arm返回值 [3]ISR内EP2 OUT事务次数(在usb_dc_usbhs.c递增) */
volatile uint32_t g_dap_dbg[4];

static volatile uint16_t USB_RequestIndexI = 0; // Request  Index In
static volatile uint16_t USB_RequestIndexO = 0; // Request  Index Out
static volatile uint16_t USB_RequestCountI = 0; // Request  Count In
static volatile uint16_t USB_RequestCountO = 0; // Request  Count Out
static volatile uint8_t USB_RequestIdle = 1;    // Request  Idle  Flag

static volatile uint16_t USB_ResponseIndexI = 0; // Response Index In
static volatile uint16_t USB_ResponseIndexO = 0; // Response Index Out
static volatile uint16_t USB_ResponseCountI = 0; // Response Count In
static volatile uint16_t USB_ResponseCountO = 0; // Response Count Out
static volatile uint8_t USB_ResponseIdle = 1;    // Response Idle  Flag

static USB_MEM_ALIGNX uint8_t USB_Request[DAP_PACKET_COUNT][DAP_PACKET_SIZE];  // Request  Buffer
static USB_MEM_ALIGNX uint8_t USB_Response[DAP_PACKET_COUNT][DAP_PACKET_SIZE]; // Response Buffer
static uint16_t USB_RespSize[DAP_PACKET_COUNT];


// 工作线程
static struct rt_thread dap_thread;
// 工作线程栈
ALIGN(RT_ALIGN_SIZE) static char dap_thread_stack[DAP_THREAD_STACK_SIZE];


/***************************************************************************/
/*                                                                         */
/*                    DAP bulk v2端点与事件(2026-10-01由bsp_usb.c移入)      */
/*                                                                         */
/***************************************************************************/

/* DAP端点描述符(厂商bulk接口无类驱动, 自备; wMaxPacketSize=DAP_USB_PACKET_SIZE) */
static struct usb_endpoint_descriptor dap_ep_out_desc = {
    .bLength = 0x07,
    .bDescriptorType = USB_DESCRIPTOR_TYPE_ENDPOINT,
    .bEndpointAddress = DAP_OUT_EP,
    .bmAttributes = USB_ENDPOINT_TYPE_BULK,
    .wMaxPacketSize = DAP_USB_PACKET_SIZE,
    .bInterval = 0,
};
static struct usb_endpoint_descriptor dap_ep_in_desc = {
    .bLength = 0x07,
    .bDescriptorType = USB_DESCRIPTOR_TYPE_ENDPOINT,
    .bEndpointAddress = DAP_IN_EP,
    .bmAttributes = USB_ENDPOINT_TYPE_BULK,
    .wMaxPacketSize = DAP_USB_PACKET_SIZE,
    .bInterval = 0,
};

void dap_event_handler(uint8_t busid, uint8_t event)
{
    (void)busid;
    switch (event) {
        case USBD_EVENT_RESET:

            break;
        case USBD_EVENT_CONFIGURED:
            /* 自开端点后挂首包(幂等, 总线复位重枚举后自愈) */
            usbd_ep_open(0, &dap_ep_out_desc);
            usbd_ep_open(0, &dap_ep_in_desc);
            USB_RequestIdle = 0;
            g_dap_dbg[0]++;
            g_dap_dbg[2] = (uint32_t)usbd_ep_start_read(0, DAP_OUT_EP, USB_Request[0], DAP_PACKET_SIZE);
            break;
        default:
            break;
    }
}

void dap_acm_bulk_out (uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void) busid;
    g_dap_dbg[1]++;
    if (USB_Request[USB_RequestIndexI][0] == ID_DAP_TransferAbort) {
        DAP_TransferAbort = 1U;
    } else {
        USB_RequestIndexI++;
        if (USB_RequestIndexI == DAP_PACKET_COUNT) {
            USB_RequestIndexI = 0U;
        }
        USB_RequestCountI++;
    }

    // Start reception of next request packet
    if ((uint16_t) (USB_RequestCountI - USB_RequestCountO) != DAP_PACKET_COUNT) {
        usbd_ep_start_read(0, DAP_OUT_EP, USB_Request[USB_RequestIndexI], DAP_PACKET_SIZE);
    } else {
        USB_RequestIdle = 1U;
    }

    rt_sem_release(&dap_handle.request_sem);
}

void dap_acm_bulk_in (uint8_t busid, uint8_t ep, uint32_t nbytes) 
{
    (void) busid;
    if (USB_ResponseCountI != USB_ResponseCountO) {
        // Load data from response buffer to be sent back
        usbd_ep_start_write(0, DAP_IN_EP, USB_Response[USB_ResponseIndexO], USB_RespSize[USB_ResponseIndexO]);
        USB_ResponseIndexO++;
        if (USB_ResponseIndexO == DAP_PACKET_COUNT) {
            USB_ResponseIndexO = 0U;
        }
        USB_ResponseCountO++;
    } else {
        USB_ResponseIdle = 1U;
    }
}

/***************************************************************************/
/*                                                                         */
/*                              DAP 工作线程                                */
/*                                                                         */
/***************************************************************************/

void dap_thread_entry(void *param)
{
    uint32_t n;
    while (1) {
        // wait for dap request
        rt_sem_take(&dap_handle.request_sem, RT_WAITING_FOREVER);

        // Process pending requests
        while (USB_RequestCountI != USB_RequestCountO) {
            // Handle Queue Commands
            n = USB_RequestIndexO;
            while (USB_Request[n][0] == ID_DAP_QueueCommands) {
                USB_Request[n][0] = ID_DAP_ExecuteCommands;
                n++;
                if (n == DAP_PACKET_COUNT) {
                    n = 0U;
                }
            }

            // Execute DAP Command (process request and prepare response)
            USB_RespSize[USB_ResponseIndexI] =
                    (uint16_t) DAP_ExecuteCommand(USB_Request[USB_RequestIndexO], USB_Response[USB_ResponseIndexI]);

            // Update Request Index and Count
            USB_RequestIndexO++;
            if (USB_RequestIndexO == DAP_PACKET_COUNT) {
                USB_RequestIndexO = 0U;
            }
            USB_RequestCountO++;

            if (USB_RequestIdle) {
                if ((uint16_t) (USB_RequestCountI - USB_RequestCountO) != DAP_PACKET_COUNT) {
                    USB_RequestIdle = 0U;
                    usbd_ep_start_read(0, DAP_OUT_EP, USB_Request[USB_RequestIndexI], DAP_PACKET_SIZE);
                }
            }

            // Update Response Index and Count
            USB_ResponseIndexI++;
            if (USB_ResponseIndexI == DAP_PACKET_COUNT) {
                USB_ResponseIndexI = 0U;
            }
            USB_ResponseCountI++;

            if (USB_ResponseIdle) {
                if (USB_ResponseCountI != USB_ResponseCountO) {
                    // Load data from response buffer to be sent back
                    n = USB_ResponseIndexO++;
                    if (USB_ResponseIndexO == DAP_PACKET_COUNT) {
                        USB_ResponseIndexO = 0U;
                    }
                    USB_ResponseCountO++;
                    USB_ResponseIdle = 0U;
                    usbd_ep_start_write(0, DAP_IN_EP, USB_Response[n], USB_RespSize[n]);
                }
            }
        }
    }
}

/***************************************************************************/
/*                                                                         */
/*                           DAP 设备注册和初始化                           */
/*                                                                         */
/***************************************************************************/

int dap_env_init(void)
{
    /* 向下注册(早于bsp_usb层INIT_ENV的usbd_initialize) + 数据面资源 */
    dap_handle.out_ep.ep_addr = DAP_OUT_EP;
    dap_handle.out_ep.ep_cb = dap_acm_bulk_out;
    dap_handle.in_ep.ep_addr = DAP_IN_EP;
    dap_handle.in_ep.ep_cb = dap_acm_bulk_in;
    dap_handle.cfg_cb = dap_event_handler;
    rt_sem_init(&dap_handle.request_sem, "dap_rx", 0, RT_IPC_FLAG_FIFO);
    return 0;
}
INIT_COMPONENT_EXPORT(dap_env_init);

int dap_thread_init(void)
{
    rt_thread_init(&dap_thread,
                   "dap",
                   dap_thread_entry,
                   RT_NULL,
                   &dap_thread_stack[0],
                   sizeof(dap_thread_stack),
                   DAP_THREAD_PRIORITY, 
                   rt_tick_from_millisecond(1));

    
    rt_thread_startup(&dap_thread);

    return 0;
}
INIT_APP_EXPORT(dap_thread_init);