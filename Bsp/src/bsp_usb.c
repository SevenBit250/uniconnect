#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "bsp_usb.h"
#include "bsp_pool.h"
#include "bsp_config.h"
#include "main.h"
#include <rthw.h>



/***************************************************************************/
/*                                                                         */
/*                          cdc out(usb->uart)块缓冲池                     */
/*                                                                         */
/*                                                                         */
/*  USB OUT DMA直接落块, 块满/短包即提交, UART侧pop取块发DMA,               */
/*  发完recycle归还; 池耗尽则不挂端点(硬件NAK背压), recycle后重挂            */
/***************************************************************************/

bsp_usb_cdc_buffer_t cdc_handle;

static struct usbd_interface cdc_intf0;
static struct usbd_interface cdc_intf1;

/* out池: 块首恒4字节对齐, 直接作为UEP4_RX_DMA落点 */
static __attribute__((aligned(4))) uint8_t cdc_out_pool_storage[USB_CDC_OUT_BLOCK_SIZE * USB_CDC_OUT_BLOCK_COUNT];
static volatile uint8_t *cdc_out_landing = RT_NULL;    // 当前OUT落块
static volatile uint8_t cdc_out_paused = 0;            // 池耗尽, OUT端点未挂(NAK)

/* 挂OUT端点读一块 (关中断内或USB ISR上下文调用) */
static void bsp_usb_cdc_arm_out(uint8_t *block)
{
    cdc_out_landing = block;
    usbd_ep_start_read(0, CDC_OUT_EP, block, USB_CDC_OUT_BLOCK_SIZE);
}

/* USB OUT完成(块满或短包): 提交旧块, 换新块续收; 池耗尽则挂起 */
static void bsp_usb_cdc_out_done(uint16_t nbytes)
{
    g_dbg.out_done++;
    g_dbg.out_bytes += nbytes;
    if (nbytes == 0) {
        /* 主机ZLP: 无数据, 原块重挂端点, 不提交不唤醒 */
        g_dbg.out_zlp++;
        bsp_usb_cdc_arm_out((uint8_t *)cdc_out_landing);
        return;
    }
    bsp_pool_submit(&cdc_handle.rx_out_pool, (uint8_t *)cdc_out_landing, nbytes);
    uint8_t *next = bsp_pool_acquire(&cdc_handle.rx_out_pool);
    if (next != RT_NULL) {
        bsp_usb_cdc_arm_out(next);
    } else {
        cdc_out_paused = 1;
        g_dbg.out_pause++;
    }
    rt_sem_release(&cdc_handle.data_ready_sem);
}

/* bsp内部OUT完成处理, 再转发App事件 */
static void bsp_usb_cdc_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)ep;
    bsp_usb_cdc_out_done((uint16_t)nbytes);
}

uint8_t bsp_usb_cdc_rx_pop(uint8_t **block, uint16_t *len)
{
    return bsp_pool_pop(&cdc_handle.rx_out_pool, block, len);
}

void bsp_usb_cdc_rx_recycle(uint8_t *block)
{
    bsp_pool_recycle(&cdc_handle.rx_out_pool, block);
}

void bsp_usb_cdc_rx_try_resume(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (cdc_out_paused) {
        uint8_t *next = bsp_pool_acquire(&cdc_handle.rx_out_pool);
        if (next != RT_NULL) {
            bsp_usb_cdc_arm_out(next);
            cdc_out_paused = 0;
            g_dbg.out_resume_ok++;
        } else {
            g_dbg.out_resume_fail++;
        }
    }
    rt_hw_interrupt_enable(level);
}

void bsp_usb_cdc_rx_reset(uint8_t *uart_inflight)
{
    rt_base_t level = rt_hw_interrupt_disable();
    uint8_t *except[1];
    uint8_t except_n = 0;
    if (uart_inflight != RT_NULL) {
        except[0] = uart_inflight;
        except_n = 1;
    }
    bsp_pool_reset(&cdc_handle.rx_out_pool, except, except_n);
    uint8_t *first = bsp_pool_acquire(&cdc_handle.rx_out_pool);
    if (first != RT_NULL) {
        bsp_usb_cdc_arm_out(first);
        cdc_out_paused = 0;
    } else {
        cdc_out_paused = 1;
    }
    rt_hw_interrupt_enable(level);
}

/***************************************************************************/
/*                                                                         */
/*                          cdc in(uart->usb)发送                          */
/*                                                                         */
/***************************************************************************/

int bsp_usb_cdc_ep_start_write(uint8_t busid, const uint8_t *data, uint32_t data_len)
{
    return usbd_ep_start_write(busid, CDC_IN_EP, data, data_len);
}

int bsp_usb_cdc_get_mps(void)
{
    return USB_CDC_OUT_BLOCK_SIZE;
}

/***************************************************************************/
/*                                                                         */
/*                                   dap相关                               */
/*                                                                         */
/***************************************************************************/
bsp_usb_dap_buffer_t dap_handle;

/***************************************************************************/
/*                                                                         */
/*                                   usb总线配置                           */
/*                                                                         */
/***************************************************************************/

/*!< config descriptor size */
#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN)

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT (USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01)
};

static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT (USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT (0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, CDC_MAX_MPS, 0x02)
};

static const uint8_t device_quality_descriptor[] = {
    0x0a,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00,
    0x02,
    0x00,
    0x00,
    0x00,
    0x40,
    0x00,
    0x00,
};

static const char *string_descriptors[] = {
    (const char[]){0x09, 0x04}, /* Langid */
    "CherryUSB", /* Manufacturer */
    "CherryUSB CDC DEMO", /* Product */
    "2022123456", /* Serial Number */
};

static const uint8_t *device_descriptor_callback (uint8_t speed) {
    return device_descriptor;
}

static const uint8_t *config_descriptor_callback (uint8_t speed) {
    return config_descriptor;
}

static const uint8_t *device_quality_descriptor_callback (uint8_t speed) {
    return device_quality_descriptor;
}

static const char *string_descriptor_callback (uint8_t speed, uint8_t index) {
    if (index > 3) {
        return NULL;
    }
    return string_descriptors[index];
}

const struct usb_descriptor usb_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback
};

static void usbd_event_handler (uint8_t busid, uint8_t event) {
    if (event == USBD_EVENT_CONFIGURED) {
        updateSystemStatus(SYS_STA_USB_CONNECTED);
        /* 重枚举: 重建OUT池并挂首块(池状态机的守卫会拒绝陈旧回收) */
        bsp_usb_cdc_rx_reset(RT_NULL);
    }
    /* cdc callback */
    if (cdc_handle.cdc_event_hander_callback) {
        cdc_handle.cdc_event_hander_callback(busid, event);
    }
}

static int bsp_usb_env_init(void)
{
    rt_sem_init(&cdc_handle.data_ready_sem, "cdc_rx", 0, RT_IPC_FLAG_FIFO);
    rt_sem_init(&cdc_handle.lincoding_sem, "cdc_ctl", 0, RT_IPC_FLAG_FIFO);
    bsp_pool_init(&cdc_handle.rx_out_pool, cdc_out_pool_storage, USB_CDC_OUT_BLOCK_SIZE, USB_CDC_OUT_BLOCK_COUNT);
    return 0;
}
INIT_COMPONENT_EXPORT(bsp_usb_env_init);

static int bsp_usb_init(void)
{
    usbd_desc_register (0, &usb_descriptor);
    /* cdc config */
    cdc_handle.in_ep.ep_addr = CDC_IN_EP;
    cdc_handle.out_ep.ep_addr = CDC_OUT_EP;
    cdc_handle.out_ep.ep_cb = bsp_usb_cdc_bulk_out;
    usbd_add_interface (0, usbd_cdc_acm_init_intf (0, &cdc_intf0));
    usbd_add_interface (0, usbd_cdc_acm_init_intf (0, &cdc_intf1));
    usbd_add_endpoint (0, &cdc_handle.out_ep);
    usbd_add_endpoint (0, &cdc_handle.in_ep);

    usbd_initialize (0, 0, usbd_event_handler);

    return 0;
}
INIT_ENV_EXPORT(bsp_usb_init);

void usb_dc_low_level_init(void)
{
    NVIC_InitTypeDef NVIC_InitStructure = {0};
    RCC_USBCLK48MConfig(RCC_USBCLK48MCLKSource_USBPHY);
    RCC_USBHSPLLCLKConfig(RCC_HSBHSPLLCLKSource_HSE);
    RCC_USBHSConfig(RCC_USBPLL_Div2);
    RCC_USBHSPLLCKREFCLKConfig( RCC_USBHSPLLCKREFCLK_4M);
    RCC_USBHSPHYPLLALIVEcmd(ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, ENABLE);
    /* 与USART/DMA同优先级(1), 默认0(最高)会抢占其他中断形成深层嵌套 */
    NVIC_InitStructure.NVIC_IRQChannel = USBHS_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}
