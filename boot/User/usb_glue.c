/* boot的USB层: 单HID接口描述符 + 端点 + 低层时钟初始化
 * 复用主工程的CherryUSB端口驱动(CONFIG_USB_BAREMETAL剥离RTOS依赖)。
 * PID 0x0219与APP(0x0218)隔离: Windows全新设备节点, 升级工具按
 * VID/PID+产品串"UniConnect Bootloader"识别(docs/bootloader-design.md)。 */
#include "ch32v30x.h"
#include "usbd_core.h"
#include "usbd_hid.h"
#include "boot.h"
#include "bootproto.h"
#include "usb_ids.h"   /* VID/PID单一事实源(common/, 与APP/SDK共用) */

#define USBD_VID_BOOT     UNI_VID
#define USBD_PID_BOOT     UNI_PID_BOOT
#define USBD_MAX_POWER    50

/* EP: 接口0, OUT=0x01 / IN=0x81, 中断传输64B/1ms */
#define BOOT_OUT_EP       0x01
#define BOOT_IN_EP        0x81

/* 共享端口驱动(usb_dc_usbhs.c)引用的APP侧观测计数器桩:
 * 仅计数用途, boot无需消费 */
volatile uint32_t g_dap_dbg[4];
volatile uint32_t g_out_tog_drop;

/* ---------------- 描述符 ---------------- */

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00,
                               USBD_VID_BOOT, USBD_PID_BOOT, 0x0100, 0x01)
};

#define BOOT_CONFIG_SIZE (9 + 9 + 9 + 7 + 7)

static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(BOOT_CONFIG_SIZE, 0x01, 0x01,
                               USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    /* HID接口: class=3, 子类/协议=0(非boot), 无接口字符串 */
    0x09, 0x04, 0x00, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,
    /* HID类描述符: 报告描述符24B(与report_descriptor严格同长) */
    0x09, 0x21, 0x01, 0x00, 0x00, 0x01, 0x22, 0x18, 0x00,
    /* 端点: OUT/IN 中断, 64B, 1ms */
    0x07, 0x05, BOOT_OUT_EP, 0x03, WBVAL(64), 0x01,
    0x07, 0x05, BOOT_IN_EP, 0x03, WBVAL(64), 0x01,
};

/* 厂商自定义报告: 64B输入 + 64B输出, 无report id
 * [坑] Usage Page是2字节项(0x05,0xFF): 曾写成0x05,0x00,0xFF(3B),
 * 多出的0x00使Windows HID解析器在0xFF长项处判废 -> usbhid Code 10
 * FAILED_START, hidapi枚举不到(2026-10-02 SETUP日志实锤) */
static const uint8_t report_descriptor[] = {
    0x05, 0xFF,              /* Usage Page (Vendor Defined 0xFF) */
    0x09, 0x01,              /* Usage (0x01) */
    0xA1, 0x01,              /* Collection (Application) */
    0x15, 0x00,              /*   Logical Minimum (0) */
    0x26, 0xFF, 0x00,        /*   Logical Maximum (255) */
    0x75, 0x08,              /*   Report Size (8) */
    0x95, 0x40,              /*   Report Count (64) */
    0x09, 0x02,              /*   Usage (0x02) */
    0x81, 0x02,              /*   Input (Data,Var,Abs) */
    0x09, 0x03,              /*   Usage (0x03) */
    0x91, 0x02,              /*   Output (Data,Var,Abs) */
    0xC0,                    /* End Collection (24B) */
};

static const uint8_t device_quality_descriptor[] = {
    0x0a, USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x40, 0x01, 0x00,
};

static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 },          /* LANGID en-US */
    "UniConnect",
    "UniConnect Bootloader",
    "UNICONNECT-BL",
};

static const uint8_t *device_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return device_descriptor;
}

static const uint8_t *config_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return config_descriptor;
}

static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return device_quality_descriptor;
}

static const uint8_t *other_speed_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return NULL;
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
    (void)speed;
    /* [坑] index==0(LANGID)必须返回string_descriptors[0]而非NULL: CherryUSB
     * 核心层对LANGID请求先要求回调非空, 再取返回前2字节合成4B字符串描述符;
     * 返回NULL=STALL -> Windows枚举止步(不发SET_CONFIG)->HID Code 10
     * (2026-10-03 SETUP环形日志实锚: 枚举停在string0后) */
    if (index >= sizeof(string_descriptors) / sizeof(string_descriptors[0])) {
        return NULL;
    }
    return string_descriptors[index];
}

const struct usb_descriptor boot_usb_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .other_speed_descriptor_callback = other_speed_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback,
};

/* ---------------- 端点数据面 ---------------- */

/* 请求队列: USB回调(ISR)投递, 主循环消费。HID主机最快1包/ms,
 * 主循环一包处理(含页编程~ms)期间至多积压数包, 深度8冗余 */
#define REQ_Q_LEN 8
static __attribute__((aligned(4))) uint8_t req_q[REQ_Q_LEN][HID_REPORT_SIZE];
static volatile uint8_t req_w = 0, req_r = 0;

/* OUT落块缓冲(单缓冲: 回调内立刻拷走并重挂) */
static __attribute__((aligned(4))) uint8_t ep_rx_buf[HID_REPORT_SIZE];

static uint8_t rsp_buf[HID_REPORT_SIZE];
static volatile uint8_t rsp_idle = 1;

static void boot_ep_out_cb(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid; (void)ep;
    if (nbytes && nbytes <= HID_REPORT_SIZE) {
        uint8_t nx = (req_w + 1) % REQ_Q_LEN;
        if (nx != req_r) {
            memcpy(req_q[req_w], ep_rx_buf, nbytes);
            if (nbytes < HID_REPORT_SIZE) {
                memset(&req_q[req_w][nbytes], 0, HID_REPORT_SIZE - nbytes);
            }
            req_w = nx;
        }
    }
    usbd_ep_start_read(0, BOOT_OUT_EP, ep_rx_buf, HID_REPORT_SIZE);
}

static void boot_ep_in_cb(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid; (void)ep; (void)nbytes;
    rsp_idle = 1;
}

static struct usbd_interface hid_intf;
static struct usbd_endpoint boot_out_ep = { .ep_addr = BOOT_OUT_EP, .ep_cb = boot_ep_out_cb };
static struct usbd_endpoint boot_in_ep  = { .ep_addr = BOOT_IN_EP,  .ep_cb = boot_ep_in_cb };

/* 端点描述符(CONFIGURED时usbd_ep_open用, 参照APP各数据面) */
static struct usb_endpoint_descriptor boot_out_ep_desc = {
    .bLength = 0x07,
    .bDescriptorType = USB_DESCRIPTOR_TYPE_ENDPOINT,
    .bEndpointAddress = BOOT_OUT_EP,
    .bmAttributes = USB_ENDPOINT_TYPE_INTERRUPT,
    .wMaxPacketSize = HID_REPORT_SIZE,
    .bInterval = 1,
};
static struct usb_endpoint_descriptor boot_in_ep_desc = {
    .bLength = 0x07,
    .bDescriptorType = USB_DESCRIPTOR_TYPE_ENDPOINT,
    .bEndpointAddress = BOOT_IN_EP,
    .bmAttributes = USB_ENDPOINT_TYPE_INTERRUPT,
    .wMaxPacketSize = HID_REPORT_SIZE,
    .bInterval = 1,
};

/* 与APP的bsp_usb同款时钟部分; NVIC部分不要 -- 裸机boot轮询驱动USB
 * (主循环查INT_FG直调USBD_IRQHandler, 见boot_usb_poll): 中断派发机制
 * 依赖(WCH-fast属性/硬件栈等)在裸机下未证实, 轮询彻底绕开(2026-10-02) */
void usb_dc_low_level_init(void)
{
    RCC_USBCLK48MConfig(RCC_USBCLK48MCLKSource_USBPHY);
    RCC_USBHSPLLCLKConfig(RCC_HSBHSPLLCLKSource_HSE);
    RCC_USBHSConfig(RCC_USBPLL_Div2);
    RCC_USBHSPLLCKREFCLKConfig(RCC_USBHSPLLCKREFCLK_4M);
    RCC_USBHSPHYPLLALIVEcmd(ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, ENABLE);
    NVIC_DisableIRQ(USBHS_IRQn);
}

/* EP0 SETUP包环形日志(诊断, 裸地址RAM 0x2000C800, openocd读):
 * 8组x8字节, 组内=setup包原样; [64]=写指针 */
#define SETUP_LOG_ADDR ((volatile uint8_t *)0x2000C800u)
static volatile uint8_t setup_log_wr;
extern uint32_t g_ch32_usbhs_udc;   /* 端口驱动全局, 首成员=setup包 */

static void log_setup_if_any(uint8_t fg)
{
    if (fg & 0x20) {   /* USBHS_SETUP_ACT */
        volatile uint8_t *src = (volatile uint8_t *)&g_ch32_usbhs_udc;
        volatile uint8_t *dst = SETUP_LOG_ADDR + (uint8_t)(setup_log_wr++ & 7) * 8;
        for (uint8_t i = 0; i < 8; i++) dst[i] = src[i];
        SETUP_LOG_ADDR[64] = setup_log_wr;
    }
}

/* 主循环轮询: INT_FG非零即在非中断上下文处理USB事件(USBD_IRQHandler
 * 本体是普通函数, 仅包装器USBHS_IRQHandler带中断属性) */
void boot_usb_pump(void)
{
    extern void USBD_IRQHandler(uint8_t busid);
    SETUP_LOG_ADDR[65]++;                    /* pump存活计数(诊断) */
    uint8_t fg = USBHSD->INT_FG;
    if (fg & USBHSD->INT_EN) {
        log_setup_if_any(fg);
        USBD_IRQHandler(0);
    }

    if (req_r == req_w) return;

    uint8_t len = bootproto_handle(req_q[req_r], rsp_buf);
    req_r = (req_r + 1) % REQ_Q_LEN;
    (void)len;
    if (rsp_idle) {
        rsp_idle = 0;
        usbd_ep_start_write(0, BOOT_IN_EP, rsp_buf, HID_REPORT_SIZE);
    }
}

static void boot_usbd_event_handler(uint8_t busid, uint8_t event)
{
    (void)busid;
    if (event == USBD_EVENT_CONFIGURED) {
        usbd_ep_open(0, &boot_out_ep_desc);
        usbd_ep_open(0, &boot_in_ep_desc);
        usbd_ep_start_read(0, BOOT_OUT_EP, ep_rx_buf, HID_REPORT_SIZE);
        rsp_idle = 1;
    }
}

void boot_usb_init(void)
{
    usbd_desc_register(0, &boot_usb_descriptor);
    usbd_add_interface(0, usbd_hid_init_intf(0, &hid_intf, report_descriptor,
                                             sizeof(report_descriptor)));
    usbd_add_endpoint(0, &boot_out_ep);
    usbd_add_endpoint(0, &boot_in_ep);
    usbd_initialize(0, 0, boot_usbd_event_handler);
}
