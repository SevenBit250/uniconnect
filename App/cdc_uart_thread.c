/**
 * @file cdc_uart_thread.c
 * @author awei (heqiweii@163.com)
 * @brief cdc交互
 * @version 0.1
 * @date 2026-01-09
 * 
 * @copyright Copyright (c) 2026
 * 
 * cdc_rx_thread_entry: cdc rx -> uart tx
 * cdc_tx_thread_entry: uart rx -> cdc tx
 */
#include "config.h"
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "rtthread.h"
#include "completion.h"
#include "chry_ringbuffer.h"
#include "bsp.h"

// cdc和uart句柄
extern bsp_uart_buffer_t uart_handle;
extern bsp_usb_cdc_buffer_t cdc_handle;
// cdc行设置
static struct cdc_line_coding lincoding;
// 工作线程
static struct rt_thread cdc_tx_thread;
static struct rt_thread cdc_rx_thread;
static struct rt_thread cdc_ctrl_thread;
static struct rt_thread cdc_sta_led_thread;
// 工作线程栈
ALIGN(RT_ALIGN_SIZE) static char cdc_tx_stack[CDC_TX_THREAD_STACK_SIZE];
ALIGN(RT_ALIGN_SIZE) static char cdc_rx_stack[CDC_RX_THREAD_STACK_SIZE];
ALIGN(RT_ALIGN_SIZE) static char cdc_ctrl_stack[CDC_CTRL_THREAD_STACK_SIZE];
ALIGN(RT_ALIGN_SIZE) static char cdc_sta_led_stack[CDC_STA_LED_THREAD_STACK_SIZE];


/***************************************************************************/
/*                                                                         */
/*                           CherryUSB 回调和端点                           */
/*                                                                         */
/***************************************************************************/
void cdc_uart_event_handler(uint8_t busid, uint8_t event)
{
    switch (event) {
        case USBD_EVENT_RESET:
            cdc_handle.allow_transfer = RT_FALSE;
            break;
        case USBD_EVENT_CONFIGURED:
            bsp_usb_cdc_ep_start_read(busid);
            break;
        default:
            break;
    }
}

void cdc_uart_acm_bulk_out (uint8_t busid, uint8_t ep, uint32_t nbytes) 
{
    (void)busid;
    chry_ringbuffer_write(&cdc_handle.rb_cdc_rx, bsp_usb_cdc_get_buffer(), nbytes);
    if (chry_ringbuffer_get_free(&cdc_handle.rb_cdc_rx) >= bsp_usb_cdc_get_buffer_size()) {
        bsp_usb_cdc_ep_start_read(busid);
    } else {
        cdc_handle.rx_buffer_full = RT_TRUE;
    }
    rt_sem_release(&cdc_handle.data_ready_sem);
}

void cdc_uart_acm_bulk_in (uint8_t busid, uint8_t ep, uint32_t nbytes) 
{
    (void)busid;
    uint32_t size;
    uint8_t *buffer;
    chry_ringbuffer_linear_read_done(&uart_handle.rb_uart_rx, nbytes);
    if ((nbytes % bsp_usb_cdc_get_buffer_size()) == 0 && nbytes) {
        /* send zlp */
        bsp_usb_cdc_ep_start_write(busid, NULL, 0);
    } else {
        if (chry_ringbuffer_get_used(&uart_handle.rb_uart_rx)) {
            buffer = chry_ringbuffer_linear_read_setup(&uart_handle.rb_uart_rx, &size);
            bsp_usb_cdc_ep_start_write(busid, buffer, size);
        } else {
            cdc_handle.tx_buffer_empty = RT_TRUE;
        }
    }
}

void usbd_cdc_acm_set_line_coding (uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding) 
{
    (void)busid;
    if (memcmp (line_coding, (uint8_t *)&lincoding, sizeof (struct cdc_line_coding)) != 0) {
        memcpy ((uint8_t *)&lincoding, line_coding, sizeof (struct cdc_line_coding));
        cdc_handle.allow_transfer = RT_FALSE;
        rt_sem_release(&cdc_handle.lincoding_sem);
    }
}

void usbd_cdc_acm_get_line_coding (uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding) 
{
    (void)busid;
    memcpy (line_coding, (uint8_t *)&lincoding, sizeof (struct cdc_line_coding));
}

/***************************************************************************/
/*                                                                         */
/*                              dma 发送回调                                */
/*                                                                         */
/***************************************************************************/

void on_uart_tx_dma_completed(uint16_t size) {
    uint8_t *buffer;
    uint32_t txsize;
    chry_ringbuffer_linear_read_done(&cdc_handle.rb_cdc_rx, size);
    if (chry_ringbuffer_get_used(&cdc_handle.rb_cdc_rx)) {
        buffer = chry_ringbuffer_linear_read_setup(&cdc_handle.rb_cdc_rx, &txsize);
        bsp_uart_write_by_dma(buffer, txsize);
    } else {
        rt_sem_release (&uart_handle.tx_complete_sem);
    }
}

/***************************************************************************/
/*                                                                         */
/*                              CDC 工作线程                                */
/*                                                                         */
/***************************************************************************/

void cdc_tx_thread_entry(void *param)
{
    uint32_t size;
    uint8_t *buffer;
    while (1) {
        rt_sem_take(&uart_handle.data_ready_sem, RT_WAITING_FOREVER);

        if (cdc_handle.allow_transfer == RT_FALSE) {
            continue;
        }

        if (cdc_handle.tx_buffer_empty == RT_FALSE) {
            continue;
        }

        if (chry_ringbuffer_get_used(&uart_handle.rb_uart_rx)) {
            cdc_handle.tx_buffer_empty = RT_FALSE;
            buffer = chry_ringbuffer_linear_read_setup (&uart_handle.rb_uart_rx, &size);
            bsp_usb_cdc_ep_start_write(0, buffer, size);
        }
    }
}

void cdc_rx_thread_entry(void *param)
{
    uint32_t size;
    uint8_t *buffer;
    while (1) {
        rt_sem_take(&cdc_handle.data_ready_sem, RT_WAITING_FOREVER);

        if (cdc_handle.allow_transfer == RT_FALSE) {
            continue;
        }

        if (chry_ringbuffer_get_used(&cdc_handle.rb_cdc_rx)) {
            cdc_handle.uart_tx_working = RT_TRUE;

            buffer = chry_ringbuffer_linear_read_setup(&cdc_handle.rb_cdc_rx, &size);
            // 启动dma传输
            bsp_uart_write_by_dma(buffer, size);
            // 等待dma传输完成
            bsp_uart_wait_dma_write_completed();

            cdc_handle.uart_tx_working = RT_FALSE;
        }

        if (cdc_handle.rx_buffer_full == RT_FALSE) {
            continue;
        }

        if (chry_ringbuffer_get_free(&cdc_handle.rb_cdc_rx) >= bsp_usb_cdc_get_buffer_size()) {
            cdc_handle.rx_buffer_full = RT_FALSE;
            bsp_usb_cdc_ep_start_read(0);
        }
    }
}

void cdc_ctrl_thread_entry(void *param)
{
    while (1) {
        rt_sem_take(&cdc_handle.lincoding_sem, RT_WAITING_FOREVER);
        // 更新串口配置
        bsp_uart_config (lincoding.dwDTERate, lincoding.bCharFormat, lincoding.bParityType);
        // 重置串口缓冲
        cdc_handle.rx_buffer_full = RT_FALSE;
        cdc_handle.tx_buffer_empty = RT_TRUE;
        cdc_handle.uart_tx_working = RT_FALSE;
        cdc_handle.uart_rx_working = RT_FALSE;
        chry_ringbuffer_reset(&cdc_handle.rb_cdc_rx);
        chry_ringbuffer_reset(&uart_handle.rb_uart_rx);
        // 允许传输
        cdc_handle.allow_transfer = RT_TRUE;
        // 亮起uart指示灯
        for(uint8_t i = 0; i < 2; i++) {
            bsp_led_ctrl(BSP_LED_ID_COM_STA, 1);
            rt_thread_mdelay(50);
            bsp_led_ctrl(BSP_LED_ID_COM_STA, 0);
            rt_thread_mdelay(50);
        }
    }
}

void cdc_sta_led_thread_entry(void *param)
{
    while (1) {
        rt_thread_mdelay(1);
        if (cdc_handle.allow_transfer == RT_FALSE) {
            continue;
        }
        if (!(cdc_handle.uart_tx_working || cdc_handle.uart_rx_working)) {
            bsp_led_ctrl(BSP_LED_ID_COM_STA, 0);
            continue;
        }
        bsp_led_toggle(BSP_LED_ID_COM_STA);
    }
}

/***************************************************************************/
/*                                                                         */
/*                           CDC 设备注册和初始化                           */
/*                                                                         */
/***************************************************************************/

int cdc_uart_env_init(void)
{
    uart_handle.write_completed_callback = on_uart_tx_dma_completed;

    cdc_handle.in_ep.ep_cb = cdc_uart_acm_bulk_in;
    cdc_handle.out_ep.ep_cb = cdc_uart_acm_bulk_out;
    cdc_handle.cdc_event_hander_callback = cdc_uart_event_handler;
    return 0;
}
INIT_COMPONENT_EXPORT(cdc_uart_env_init);

int cdc_uart_thread_init(void)
{
    rt_thread_init(&cdc_tx_thread,
                   "cdc_tx",
                   cdc_tx_thread_entry,
                   RT_NULL,
                   &cdc_tx_stack[0],
                   sizeof(cdc_tx_stack),
                   CDC_TX_THREAD_PRIORITY, 
                   rt_tick_from_millisecond(1));

    rt_thread_init(&cdc_rx_thread,
                   "cdc_rx",
                   cdc_rx_thread_entry,
                   RT_NULL,
                   &cdc_rx_stack[0],
                   sizeof(cdc_rx_stack),
                   CDC_RX_THREAD_PRIORITY, 
                   rt_tick_from_millisecond(1));

    rt_thread_init(&cdc_ctrl_thread,
                   "cdc_ctrl",
                   cdc_ctrl_thread_entry,
                   RT_NULL,
                   &cdc_ctrl_stack[0],
                   sizeof(cdc_ctrl_stack),
                   CDC_CTRL_THREAD_PRIORITY, 
                   rt_tick_from_millisecond(1));

    rt_thread_init(&cdc_sta_led_thread,
                   "cdc_sta",
                   cdc_sta_led_thread_entry,
                   RT_NULL,
                   &cdc_sta_led_stack[0],
                   sizeof(cdc_sta_led_stack),
                   CDC_STA_LED_THREAD_PRIORITY, 
                   rt_tick_from_millisecond(1));
    
    rt_thread_startup(&cdc_tx_thread);

    rt_thread_startup(&cdc_rx_thread);

    rt_thread_startup(&cdc_ctrl_thread);

    rt_thread_startup(&cdc_sta_led_thread);

    return 0;
}
INIT_APP_EXPORT(cdc_uart_thread_init);