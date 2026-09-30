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
/*                     can-usb桥 out(usb->can)块缓冲池                     */
/*         与CDC OUT同模式: USB落块, 桥接线程pop解析, 池尽NAK背压          */
/***************************************************************************/

bsp_usb_can_buffer_t canusb_handle;

static __attribute__((aligned(4))) uint8_t canusb_out_pool_storage[CAN_USB_BLOCK_SIZE * CAN_USB_OUT_BLOCKS];
static volatile uint8_t *canusb_out_landing = RT_NULL;
static volatile uint8_t canusb_out_paused = 0;

static void bsp_can_usb_arm_out(uint8_t *block)
{
    canusb_out_landing = block;
    usbd_ep_start_read(0, CAN_IF_OUT_EP, block, CAN_USB_BLOCK_SIZE);
}

/* CAN端点开启(CONFIGURED事件, 总线复位重枚举后重开) */
/* CAN桥端点描述符(供usbd_ep_open, 无类驱动须自备) */
static struct usb_endpoint_descriptor can_ep_out_desc = {
    .bLength = 0x07,
    .bDescriptorType = USB_DESCRIPTOR_TYPE_ENDPOINT,
    .bEndpointAddress = CAN_IF_OUT_EP,
    .bmAttributes = USB_ENDPOINT_TYPE_BULK,
    .wMaxPacketSize = CAN_USB_BLOCK_SIZE,
    .bInterval = 0,
};
static struct usb_endpoint_descriptor can_ep_in_desc = {
    .bLength = 0x07,
    .bDescriptorType = USB_DESCRIPTOR_TYPE_ENDPOINT,
    .bEndpointAddress = CAN_IF_IN_EP,
    .bmAttributes = USB_ENDPOINT_TYPE_BULK,
    .wMaxPacketSize = CAN_USB_BLOCK_SIZE,
    .bInterval = 0,
};

void bsp_can_usb_ep_open(void)
{
    /* CONFIGURED时调用(总线复位重枚举后重开, 幂等) */
    usbd_ep_open(0, &can_ep_out_desc);
    usbd_ep_open(0, &can_ep_in_desc);
}

static void bsp_can_usb_out_done(uint16_t nbytes)
{
    if (nbytes == 0) {
        /* 主机ZLP: 原块重挂 */
        bsp_can_usb_arm_out((uint8_t *)canusb_out_landing);
        return;
    }
    bsp_pool_submit(&canusb_handle.out_pool, (uint8_t *)canusb_out_landing, nbytes);
    uint8_t *next = bsp_pool_acquire(&canusb_handle.out_pool);
    if (next != RT_NULL) {
        bsp_can_usb_arm_out(next);
    } else {
        canusb_out_paused = 1;
    }
    /* 唤醒统一走can活动信号量(桥接线程唯一等待点) */
    extern struct rt_semaphore *bsp_can_activity_sem(void);
    rt_sem_release(bsp_can_activity_sem());
}

static void bsp_can_usb_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    bsp_can_usb_out_done((uint16_t)nbytes);
}

int bsp_can_usb_ep_start_write(uint8_t busid, const uint8_t *data, uint32_t data_len)
{
    return usbd_ep_start_write(busid, CAN_IF_IN_EP, data, data_len);
}

int bsp_can_usb_get_mps(void)
{
    return CAN_USB_BLOCK_SIZE;
}

uint8_t bsp_can_usb_rx_pop(uint8_t **block, uint16_t *len)
{
    return bsp_pool_pop(&canusb_handle.out_pool, block, len);
}

void bsp_can_usb_rx_recycle(uint8_t *block)
{
    bsp_pool_recycle(&canusb_handle.out_pool, block);
}

void bsp_can_usb_rx_try_resume(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (canusb_out_paused) {
        uint8_t *next = bsp_pool_acquire(&canusb_handle.out_pool);
        if (next != RT_NULL) {
            bsp_can_usb_arm_out(next);
            canusb_out_paused = 0;
        }
    }
    rt_hw_interrupt_enable(level);
}

void bsp_can_usb_rx_reset(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    bsp_pool_reset(&canusb_handle.out_pool, RT_NULL, 0);
    uint8_t *first = bsp_pool_acquire(&canusb_handle.out_pool);
    if (first != RT_NULL) {
        bsp_can_usb_arm_out(first);
        canusb_out_paused = 0;
    } else {
        canusb_out_paused = 1;
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

/* MSOS2.0免驱开关: 1=启用(正式版Windows免驱) 0=关闭(本机预览版调试用, Zadig手动绑定) */
#define USB_DEBUG_MSOS 0

/***************************************************************************/
/*                                                                         */
/*                                   usb总线配置                           */
/*                                                                         */
/***************************************************************************/

/*!< config descriptor size */
#define VENDOR_CAN_IF_DESC_SIZE (9 + 7 + 7)
#define CANUSB_SOLO_TEST 1   /* 单接口纯CAN设备: Zadig绑定用(复合接口子节点因Error不生成) */
#if CANUSB_SOLO_TEST
#define USB_CONFIG_SIZE (9 + VENDOR_CAN_IF_DESC_SIZE)
#else
#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN + VENDOR_CAN_IF_DESC_SIZE)
#endif

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT (USB_DEBUG_MSOS ? USB_2_1 : USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01)
};

static const uint8_t config_descriptor[] = {
#if CANUSB_SOLO_TEST
    USB_CONFIG_DESCRIPTOR_INIT (USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,
#else
    USB_CONFIG_DESCRIPTOR_INIT (USB_CONFIG_SIZE, 0x03, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT (0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, CDC_MAX_MPS, 0x02)
    ,
    0x09, 0x04, 0x02, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x02,
#endif
    0x07, 0x05, CAN_IF_OUT_EP, 0x02, 0x00, 0x02, 0x00,
    0x07, 0x05, CAN_IF_IN_EP, 0x02, 0x00, 0x02, 0x00,
};

/* MS OS 2.0描述符集(166B): 头固定10B(len/type/ver/total, 无保留字节--16B带保留
 * 会被Windows按固定10B解析出垃圾子集头而静默放弃绑定)。仅接口2(CAN)绑定WINUSB。 */
static const uint8_t msosv2_descriptor_set[] = {
    0x0A, 0x00, 0x06, 0x00, 0x00, 0x00, 0x03, 0x06, 0xAA, 0x00, 0x08, 0x00,
    0x08, 0x00, 0x02, 0x00, 0xA0, 0x00, 0x14, 0x00, 0x03, 0x00, 0x57, 0x49,
    0x4E, 0x55, 0x53, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x84, 0x00, 0x04, 0x00, 0x07, 0x00, 0x2A, 0x00, 0x44, 0x00,
    0x65, 0x00, 0x76, 0x00, 0x69, 0x00, 0x63, 0x00, 0x65, 0x00, 0x49, 0x00,
    0x6E, 0x00, 0x74, 0x00, 0x65, 0x00, 0x72, 0x00, 0x66, 0x00, 0x61, 0x00,
    0x63, 0x00, 0x65, 0x00, 0x47, 0x00, 0x55, 0x00, 0x49, 0x00, 0x44, 0x00,
    0x73, 0x00, 0x00, 0x00, 0x50, 0x00, 0x7B, 0x00, 0x39, 0x00, 0x32, 0x00,
    0x43, 0x00, 0x45, 0x00, 0x36, 0x00, 0x34, 0x00, 0x36, 0x00, 0x32, 0x00,
    0x2D, 0x00, 0x39, 0x00, 0x43, 0x00, 0x37, 0x00, 0x37, 0x00, 0x2D, 0x00,
    0x34, 0x00, 0x36, 0x00, 0x46, 0x00, 0x45, 0x00, 0x2D, 0x00, 0x39, 0x00,
    0x33, 0x00, 0x33, 0x00, 0x42, 0x00, 0x2D, 0x00, 0x33, 0x00, 0x31, 0x00,
    0x43, 0x00, 0x42, 0x00, 0x39, 0x00, 0x43, 0x00, 0x35, 0x00, 0x41, 0x00,
    0x41, 0x00, 0x33, 0x00, 0x42, 0x00, 0x39, 0x00, 0x7D, 0x00, 0x00, 0x00,
    0x00, 0x00,
};

/* BOS(33B): MS OS 2.0平台能力, vendor code 0x20 */
static const uint8_t bos_descriptor[] = {
    0x05, 0x0F, 0x28, 0x00, 0x02, 0x07, 0x10, 0x02, 0x02, 0x00, 0x00, 0x00,
    0x1C, 0x10, 0x05, 0x00, 0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,
    0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F, 0x00, 0x00, 0x03, 0x06,
    0xAA, 0x00, 0x20, 0x00,
};

static struct usb_msosv2_descriptor msosv2_desc = {
    .compat_id = msosv2_descriptor_set,
    .compat_id_len = sizeof(msosv2_descriptor_set),   /* 完整描述符集(内部wLength=0xAC一致) */
    .vendor_code = 0x20,
};

static struct usb_bos_descriptor bos_desc = {
    .string = bos_descriptor,
    .string_len = sizeof(bos_descriptor),
};

static const uint8_t device_quality_descriptor[] = {
    0x0a,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x10,   /* bcdUSB与设备描述符一致(0x0210), 不一致会被部分主机视为矛盾放弃BOS流程 */
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
    "UniConnect", /* Manufacturer */
    "UniConnect CMSIS-DAP Bridge", /* Product */
    "UNICONNECT-17", /* Serial Number: 变更可触发Windows全新安装(重跑MSOS2.0绑定) */
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
    .string_descriptor_callback = string_descriptor_callback,
#if USB_DEBUG_MSOS   /* 本机25H2预览版: MSOS读取后中止枚举, 调试期关闭; 正式版Windows验证后恢复 */
    .msosv2_descriptor = &msosv2_desc,
    .bos_descriptor = &bos_desc,
#endif
};

static void usbd_event_handler (uint8_t busid, uint8_t event) {
    if (event == USBD_EVENT_CONFIGURED) {
        updateSystemStatus(SYS_STA_USB_CONNECTED);
        /* 重枚举: 重建OUT池并挂首块(池状态机的守卫会拒绝陈旧回收) */
#if !CANUSB_SOLO_TEST
        bsp_usb_cdc_rx_reset(RT_NULL);
#endif
        bsp_can_usb_ep_open();
        bsp_can_usb_rx_reset();
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
    rt_sem_init(&canusb_handle.data_ready_sem, "can_rx", 0, RT_IPC_FLAG_FIFO);
    bsp_pool_init(&canusb_handle.out_pool, canusb_out_pool_storage, CAN_USB_BLOCK_SIZE, CAN_USB_OUT_BLOCKS);
    return 0;
}
INIT_COMPONENT_EXPORT(bsp_usb_env_init);

static struct usbd_interface can_vendor_intf;

static int bsp_usb_init(void)
{
    usbd_desc_register (0, &usb_descriptor);
    /* CAN厂商接口: SOLO测试时第一个注册=接口0 */
    memset(&can_vendor_intf, 0, sizeof(can_vendor_intf));
    usbd_add_interface (0, &can_vendor_intf);
#if !CANUSB_SOLO_TEST
    /* cdc config */
    cdc_handle.in_ep.ep_addr = CDC_IN_EP;
    cdc_handle.out_ep.ep_addr = CDC_OUT_EP;
    cdc_handle.out_ep.ep_cb = bsp_usb_cdc_bulk_out;
    usbd_add_interface (0, usbd_cdc_acm_init_intf (0, &cdc_intf0));
    usbd_add_interface (0, usbd_cdc_acm_init_intf (0, &cdc_intf1));
    usbd_add_endpoint (0, &cdc_handle.out_ep);
    usbd_add_endpoint (0, &cdc_handle.in_ep);
#endif
    canusb_handle.in_ep.ep_addr = CAN_IF_IN_EP;
    canusb_handle.out_ep.ep_addr = CAN_IF_OUT_EP;
    canusb_handle.out_ep.ep_cb = bsp_can_usb_bulk_out;
    extern void can_bridge_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes);
    canusb_handle.in_ep.ep_cb = can_bridge_bulk_in;   /* App层链式续发 */
    usbd_add_endpoint (0, &canusb_handle.out_ep);
    usbd_add_endpoint (0, &canusb_handle.in_ep);

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
