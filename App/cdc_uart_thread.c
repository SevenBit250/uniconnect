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
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "usb_class.h"
#include "bsp.h"
#include "rtthread.h"
#include "completion.h"
#include "chry_ringbuffer.h"

#define CDC_TX_THREAD_STACK_SIZE   1024u

#define CDC_RX_THREAD_STACK_SIZE   1024u

#define CDC_CTRL_THREAD_STACK_SIZE 1024u

#define CDC_STA_LED_THREAD_STACK_SIZE 256u

extern uart_buffer_t uart_handle;

static usb_cdc_buffer_t cdc_handle;

static struct rt_thread cdc_tx_thread;
static struct rt_thread cdc_rx_thread;
static struct rt_thread cdc_ctrl_thread;
static struct rt_thread cdc_sta_led_thread;

ALIGN(RT_ALIGN_SIZE) static char cdc_tx_stack[CDC_TX_THREAD_STACK_SIZE];
ALIGN(RT_ALIGN_SIZE) static char cdc_rx_stack[CDC_RX_THREAD_STACK_SIZE];
ALIGN(RT_ALIGN_SIZE) static char cdc_ctrl_stack[CDC_CTRL_THREAD_STACK_SIZE];
ALIGN(RT_ALIGN_SIZE) static char cdc_sta_led_stack[CDC_STA_LED_THREAD_STACK_SIZE];

/***************************************************************************/
/*                                                                         */
/*                           CherryUSB 回调                                */
/*                                                                         */
/***************************************************************************/
void cdc_uart_event_handler(uint8_t busid, uint8_t event)
{
    switch (event) {
        case USBD_EVENT_RESET:
            cdc_handle.allow_transfer = RT_FALSE;
            break;
        case USBD_EVENT_CONFIGURED:
            usbd_ep_start_read(0, CDC_OUT_EP, cdc_handle.cdc_buffer, sizeof(cdc_handle.cdc_buffer));
            break;
        default:
            break;
    }
}

void cdc_uart_acm_bulk_out (uint8_t busid, uint8_t ep, uint32_t nbytes) 
{
    (void)busid;
    chry_ringbuffer_write(&cdc_handle.rb, cdc_handle.cdc_buffer, nbytes);
    if (chry_ringbuffer_get_free(&cdc_handle.rb) >= sizeof(cdc_handle.cdc_buffer)) {
        usbd_ep_start_read(0, CDC_OUT_EP, cdc_handle.cdc_buffer, sizeof(cdc_handle.cdc_buffer));
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
    chry_ringbuffer_linear_read_done(&uart_handle.rb, nbytes);
    if ((nbytes % USB_CDC_BUFFER_SIZE) == 0 && nbytes) {
        /* send zlp */
        usbd_ep_start_write (0, CDC_IN_EP, NULL, 0);
    } else {
        if (chry_ringbuffer_get_used(&uart_handle.rb)) {
            buffer = chry_ringbuffer_linear_read_setup(&uart_handle.rb, &size);
            usbd_ep_start_write(0, CDC_IN_EP, buffer, size);
        } else {
            cdc_handle.tx_buffer_empty = RT_TRUE;
        }
    }
}

void usbd_cdc_acm_set_line_coding (uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding) 
{
    (void)busid;
    if (memcmp (line_coding, (uint8_t *)&cdc_handle.lincoding, sizeof (struct cdc_line_coding)) != 0) {
        memcpy ((uint8_t *)&cdc_handle.lincoding, line_coding, sizeof (struct cdc_line_coding));
        cdc_handle.allow_transfer = RT_FALSE;
        rt_sem_release(&cdc_handle.lincoding_sem);
    }
}

void usbd_cdc_acm_get_line_coding (uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding) 
{
    (void)busid;
    memcpy (line_coding, (uint8_t *)&cdc_handle.lincoding, sizeof (struct cdc_line_coding));
}

/***************************************************************************/
/*                                                                         */
/*                              dma 发送回调                                */
/*                                                                         */
/***************************************************************************/

void on_uart_tx_dma_completed(uint16_t size) {
    uint8_t *buffer;
    uint32_t txsize;
    chry_ringbuffer_linear_read_done(&cdc_handle.rb, size);
    if (chry_ringbuffer_get_used(&cdc_handle.rb)) {
        buffer = chry_ringbuffer_linear_read_setup(&cdc_handle.rb, &txsize);
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

        if (chry_ringbuffer_get_used(&uart_handle.rb)) {
            cdc_handle.tx_buffer_empty = RT_FALSE;
            buffer = chry_ringbuffer_linear_read_setup (&uart_handle.rb, &size);
            usbd_ep_start_write (0, CDC_IN_EP, buffer, size);
            // rt_kprintf("buck in size: %d  ret: %d\r\n", size, ret);
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

        if (chry_ringbuffer_get_used(&cdc_handle.rb)) {
            cdc_handle.uart_tx_working = RT_TRUE;

            buffer = chry_ringbuffer_linear_read_setup(&cdc_handle.rb, &size);
            // 启动dma传输
            bsp_uart_write_by_dma(buffer, size);
            // 等待dma传输完成
            bsp_uart_wait_tx_dma_completed();

            cdc_handle.uart_tx_working = RT_FALSE;
        }

        if (cdc_handle.rx_buffer_full == RT_FALSE) {
            continue;
        }

        if (chry_ringbuffer_get_free(&cdc_handle.rb) >= sizeof(cdc_handle.cdc_buffer)) {
            cdc_handle.rx_buffer_full = RT_FALSE;
            usbd_ep_start_read(0, CDC_OUT_EP, cdc_handle.cdc_buffer, sizeof(cdc_handle.cdc_buffer));
        }
    }
}

void cdc_ctrl_thread_entry(void *param)
{
    while (1) {
        rt_sem_take(&cdc_handle.lincoding_sem, RT_WAITING_FOREVER);
        // 更新串口配置
        bsp_uart_config (
            cdc_handle.lincoding.dwDTERate, 
            cdc_handle.lincoding.bCharFormat, 
            cdc_handle.lincoding.bParityType);
        // 重置串口缓冲
        cdc_handle.rx_buffer_full = RT_FALSE;
        cdc_handle.tx_buffer_empty = RT_TRUE;
        cdc_handle.uart_tx_working = RT_FALSE;
        cdc_handle.uart_rx_working = RT_FALSE;
        chry_ringbuffer_reset(&cdc_handle.rb);
        chry_ringbuffer_reset(&uart_handle.rb);
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
    uart_handle.dma_tx_complate_callback = on_uart_tx_dma_completed;

    rt_sem_init(&cdc_handle.lincoding_sem,   "cdc_lsem", 0, RT_IPC_FLAG_FIFO);
    rt_sem_init(&cdc_handle.data_ready_sem,  "cdc_rsem", 0, RT_IPC_FLAG_FIFO);
    chry_ringbuffer_init(&cdc_handle.rb,  cdc_handle.rb_buffer,  sizeof(cdc_handle.rb_buffer));
    return 0;
}
INIT_ENV_EXPORT(cdc_uart_env_init);

int cdc_uart_thread_init(void)
{
    rt_thread_init(&cdc_tx_thread,
                   "cdc_tx",
                   cdc_tx_thread_entry,
                   RT_NULL,
                   &cdc_tx_stack[0],
                   sizeof(cdc_tx_stack),
                   10, 
                   rt_tick_from_millisecond(1));

    rt_thread_init(&cdc_rx_thread,
                   "cdc_rx",
                   cdc_rx_thread_entry,
                   RT_NULL,
                   &cdc_rx_stack[0],
                   sizeof(cdc_rx_stack),
                   10, 
                   rt_tick_from_millisecond(1));

    rt_thread_init(&cdc_ctrl_thread,
                   "cdc_ctrl",
                   cdc_ctrl_thread_entry,
                   RT_NULL,
                   &cdc_ctrl_stack[0],
                   sizeof(cdc_ctrl_stack),
                   9, 
                   rt_tick_from_millisecond(1));

    rt_thread_init(&cdc_sta_led_thread,
                   "cdc_sta",
                   cdc_sta_led_thread_entry,
                   RT_NULL,
                   &cdc_sta_led_stack[0],
                   sizeof(cdc_sta_led_stack),
                   21, 
                   rt_tick_from_millisecond(1));
    
    rt_thread_startup(&cdc_tx_thread);

    rt_thread_startup(&cdc_rx_thread);

    rt_thread_startup(&cdc_ctrl_thread);

    rt_thread_startup(&cdc_sta_led_thread);

    usb_init(0, 0);

    return 0;
}
INIT_APP_EXPORT(cdc_uart_thread_init);