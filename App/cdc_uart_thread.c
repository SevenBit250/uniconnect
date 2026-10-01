/**
 * @file cdc_uart_thread.c
 * @author awei (heqiweii@163.com)
 * @brief cdc交互
 * @version 0.2
 * @date 2026-09-29
 *
 * @copyright Copyright (c) 2026
 *
 * 双向块缓冲零拷贝:
 *   uart->usb: cdc_tx_thread起步发送, bulk_in回调零唤醒续传+回收
 *   usb->uart: cdc_rx_thread取块直发UART DMA, 发完归还+续读
 */
#include "config.h"
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "rtthread.h"
#include "rthw.h"
#include "bsp.h"

// cdc和uart句柄
extern bsp_uart_buffer_t uart_handle;
extern bsp_usb_cdc_buffer_t cdc_handle;
// cdc行设置
static struct cdc_line_coding lincoding;
// 工作线程(tx发送/rx接收/ctrl线控; LED已并入monitor统一管理)
static struct rt_thread cdc_tx_thread;
static struct rt_thread cdc_rx_thread;
static struct rt_thread cdc_ctrl_thread;
// 工作线程栈
ALIGN(RT_ALIGN_SIZE) static char cdc_tx_stack[CDC_TX_THREAD_STACK_SIZE];
ALIGN(RT_ALIGN_SIZE) static char cdc_rx_stack[CDC_RX_THREAD_STACK_SIZE];
ALIGN(RT_ALIGN_SIZE) static char cdc_ctrl_stack[CDC_CTRL_THREAD_STACK_SIZE];

// 在途跟踪: 线控reset时作为例外保留, 由各自完成路径归还
static uart_rx_desc_t cdc_tx_inflight;                 // USB IN在途(uart->usb), staged暂存槽
static volatile uint8_t cdc_tx_inflight_valid = 0;
static volatile uint8_t *cdc_uart_tx_block = RT_NULL;  // UART TX在途(usb->uart)


/***************************************************************************/
/*                                                                         */
/*                           CherryUSB 回调                                 */
/*                                                                         */
/***************************************************************************/
void cdc_uart_event_handler(uint8_t busid, uint8_t event)
{
    (void)busid;
    switch (event) {
        case USBD_EVENT_RESET:
            cdc_handle.allow_transfer = RT_FALSE;
            break;
        case USBD_EVENT_CONFIGURED:
            /* Windows在打开串口/错误恢复时会发USB总线复位, 复位后不会再发
             * 线控变更(set_line_coding被memcmp去重), allow_transfer会永久关闭.
             * 已有有效线控时释放信号量, 由ctrl线程重建串口/双向池并重新放行 */
            if (lincoding.dwDTERate != 0) {
                rt_sem_release(&cdc_handle.lincoding_sem);
            }
            break;
        default:
            break;
    }
}

/* USB IN完成(uart->usb): 归还在途块, ZLP或零唤醒续发下一块 (USB ISR上下文) */
void cdc_uart_acm_bulk_in (uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)ep;
    uart_rx_desc_t d;
    g_dbg.in_bytes += nbytes;
    if (cdc_tx_inflight_valid) {
        cdc_tx_inflight_valid = 0;
        bsp_uart_rx_release(&cdc_tx_inflight);
        g_dbg.in_recycle++;
    }
    if (nbytes && (nbytes % bsp_usb_cdc_get_mps()) == 0) {
        /* 满512整块必须发ZLP: 主机usbser只在传输边界(短包)完成读取,
         * 不发ZLP数据会滞留在主机驱动里不投递 */
        g_dbg.in_zlp++;
        bsp_usb_cdc_ep_start_write(busid, NULL, 0);
    } else if (bsp_uart_rx_pop_coalesce(&d) || (bsp_uart_rx_drain(0), bsp_uart_rx_pop_coalesce(&d))) {
        /* pop空时主动收割一次再取(贪心发送, 不等里程碑中断) */
        g_dbg.in_pop++;
        cdc_tx_inflight = d;
        cdc_tx_inflight_valid = 1;
        int ret = bsp_usb_cdc_ep_start_write(busid, d.ptr, d.len);
        if (ret != 0) {
            /* 发起失败必须回滚, 否则永远等不到bulk_in而永久死锁 */
            g_dbg.in_write_err++;
            g_dbg.in_write_ret = (uint32_t)ret;
            g_dbg.in_write_ptr = (uint32_t)d.ptr;
            cdc_tx_inflight_valid = 0;
            bsp_uart_rx_release(&cdc_tx_inflight);
            cdc_handle.tx_buffer_empty = RT_TRUE;
        }
    } else {
        cdc_handle.tx_buffer_empty = RT_TRUE;
    }
}

void usbd_cdc_acm_set_line_coding (uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding)
{
    (void)busid;
    (void)intf;
    if (memcmp (line_coding, (uint8_t *)&lincoding, sizeof (struct cdc_line_coding)) != 0) {
        memcpy ((uint8_t *)&lincoding, line_coding, sizeof (struct cdc_line_coding));
        cdc_handle.allow_transfer = RT_FALSE;
        rt_sem_release(&cdc_handle.lincoding_sem);
    }
}

void usbd_cdc_acm_get_line_coding (uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding)
{
    (void)busid;
    (void)intf;
    memcpy (line_coding, (uint8_t *)&lincoding, sizeof (struct cdc_line_coding));
}

/***************************************************************************/
/*                                                                         */
/*                              CDC 工作线程                                */
/*                                                                         */
/***************************************************************************/

/* uart->usb: 起步发送, 后续块在bulk_in回调中续传 */
void cdc_tx_thread_entry(void *param)
{
    uart_rx_desc_t d;
    while (1) {
        rt_sem_take(&uart_handle.data_ready_sem, RT_WAITING_FOREVER);

        if (cdc_handle.allow_transfer == RT_FALSE) {
            continue;
        }
        if (cdc_handle.tx_buffer_empty == RT_FALSE) {
            continue;
        }
        if (bsp_uart_rx_pop_coalesce(&d) || (bsp_uart_rx_drain(0), bsp_uart_rx_pop_coalesce(&d))) {
            g_dbg.in_pop++;
            cdc_handle.tx_buffer_empty = RT_FALSE;
            cdc_tx_inflight = d;
            cdc_tx_inflight_valid = 1;
            int ret = bsp_usb_cdc_ep_start_write(0, d.ptr, d.len);
            if (ret != 0) {
                g_dbg.in_write_err++;
                g_dbg.in_write_ret = (uint32_t)ret;
                g_dbg.in_write_ptr = (uint32_t)d.ptr;
                cdc_tx_inflight_valid = 0;
                bsp_uart_rx_release(&cdc_tx_inflight);
                cdc_handle.tx_buffer_empty = RT_TRUE;   /* 回滚避免死锁 */
            }
        }
    }
}

/* usb->uart: 取OUT块直发UART DMA, 发完归还, 池满时OUT端点NAK背压 */
void cdc_rx_thread_entry(void *param)
{
    uint8_t *block;
    uint16_t len;
    while (1) {
        rt_sem_take(&cdc_handle.data_ready_sem, RT_WAITING_FOREVER);

        if (cdc_handle.allow_transfer == RT_FALSE) {
            continue;
        }

        while (bsp_usb_cdc_rx_pop(&block, &len)) {
            if (len == 0) {
                /* 防御: 空块直接归还 */
                bsp_usb_cdc_rx_recycle(block);
                continue;
            }
            cdc_uart_tx_block = block;
            bsp_uart_write_by_dma(block, len);
            bsp_uart_wait_dma_write_completed();
            cdc_uart_tx_block = RT_NULL;
            bsp_usb_cdc_rx_recycle(block);
            bsp_usb_cdc_rx_try_resume();

            if (cdc_handle.allow_transfer == RT_FALSE) {
                break;
            }
        }
    }
}

void cdc_ctrl_thread_entry(void *param)
{
    while (1) {
        rt_sem_take(&cdc_handle.lincoding_sem, RT_WAITING_FOREVER);
        /* 调试: 魔法波特率 -> 回传计数器快照, 不动串口与缓冲 */
        if (lincoding.dwDTERate == 1234567) {
            static __attribute__((aligned(4))) uint8_t snap[sizeof(bsp_dbg_t)];
            memcpy(snap, &g_dbg, sizeof(bsp_dbg_t));
            cdc_handle.allow_transfer = RT_TRUE;
            if (cdc_handle.tx_buffer_empty) {
                cdc_handle.tx_buffer_empty = RT_FALSE;
                bsp_usb_cdc_ep_start_write(0, snap, sizeof(bsp_dbg_t));
            }
            continue;
        }
        // 更新串口配置+双向重建缓冲: 必须在同一关中断区内完成,
        // USART重配置(UE翻转)的毛刺若落在重建之外, 会触发ISR提交
        // 含乒乓旧数据的伪块(表现为下个事务的前缀垃圾)
        {
            rt_base_t level = rt_hw_interrupt_disable();
            bsp_uart_config (lincoding.dwDTERate, lincoding.bCharFormat, lincoding.bParityType);
            bsp_uart_rx_reset();
            bsp_usb_cdc_rx_reset((uint8_t *)cdc_uart_tx_block);
            rt_hw_interrupt_enable(level);
        }
        // 重置发送状态
        cdc_handle.tx_buffer_empty = RT_TRUE;
        cdc_handle.allow_transfer = RT_TRUE;
        // LED确认双闪请求(monitor线程执行, ctrl不再直接碰LED)
        cdc_handle.led_blink_req = 2;
    }
}

/***************************************************************************/
/*                                                                         */
/*                           CDC 设备注册和初始化                           */
/*                                                                         */
/***************************************************************************/

int cdc_uart_env_init(void)
{
    cdc_handle.in_ep.ep_cb = cdc_uart_acm_bulk_in;
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

    rt_thread_startup(&cdc_tx_thread);

    rt_thread_startup(&cdc_rx_thread);

    rt_thread_startup(&cdc_ctrl_thread);

    return 0;
}
INIT_APP_EXPORT(cdc_uart_thread_init);
