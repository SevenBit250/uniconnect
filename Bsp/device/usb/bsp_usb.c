#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "bsp_usb.h"
#include "bsp_config.h"
#include "main.h"
#include <string.h>

/***************************************************************************/
/*                                                                         */
/*                     USB设备层: 描述符表+复合组装+事件分发                  */
/*                                                                         */
/*  本文件只负责: 控制器初始化、设备/配置/MSOS描述符(复合布局是设备级配置)、  */
/*  按描述符顺序注册接口与端点、把总线事件分发给各功能注册的钩子。            */
/*  CDC/CAN/DAP的数据面在各自App模块, 经INIT_COMPONENT填充句柄完成注册。      */
/***************************************************************************/

bsp_usb_cdc_buffer_t cdc_handle;
bsp_usb_can_buffer_t canusb_handle;
bsp_usb_dap_buffer_t dap_handle;

/* MSOS2.0免驱开关: 1=启用(Windows 8.1+自动绑定WinUSB) 0=关闭(调试用, Zadig手动绑定) */
#define USB_DEBUG_MSOS 1

/*!< config descriptor size */
#define VENDOR_CAN_IF_DESC_SIZE (9 + 7 + 7)
#define VENDOR_DAP_IF_DESC_SIZE (9 + 7 + 7)   /* DAP bulk v2: 接口+双bulk端点 */
#define CANUSB_SOLO_TEST 0   /* 复合模式=产品配置: CDC+CAN双接口 */
#if CANUSB_SOLO_TEST
#define USB_CONFIG_SIZE (9 + VENDOR_CAN_IF_DESC_SIZE)
#else
#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN + VENDOR_CAN_IF_DESC_SIZE + VENDOR_DAP_IF_DESC_SIZE)
#endif

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT (USB_DEBUG_MSOS ? USB_2_1 : USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01)
};

static const uint8_t config_descriptor[] = {
#if CANUSB_SOLO_TEST
    USB_CONFIG_DESCRIPTOR_INIT (USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,
#else
    USB_CONFIG_DESCRIPTOR_INIT (USB_CONFIG_SIZE, 0x04, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT (0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, CDC_MAX_MPS, 0x02)
    ,
#endif
    /* DAP bulk v2(intf 2): 必须排在CAN前面 -- OpenOCD/pyOCD取第一个厂商bulk
     * 接口, 曾因CAN在前被抢走(DAP_Info发给了CAN桥)。接口字符串必须是
     * "CMSIS-DAP"(严格匹配的工具靠它识别v2, 规范2.1)。wMaxPacketSize=
     * 512=0x0200=DAP_USB_PACKET_SIZE(HS WinUSB) */
    0x09, 0x04, 0x02, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x04,
    0x07, 0x05, DAP_OUT_EP, 0x02, 0x00, 0x02, 0x00,
    0x07, 0x05, DAP_IN_EP, 0x02, 0x00, 0x02, 0x00,
    /* CAN桥(intf 3) */
    0x09, 0x04, 0x03, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x05,
    0x07, 0x05, CAN_IF_OUT_EP, 0x02, 0x00, 0x02, 0x00,
    0x07, 0x05, CAN_IF_IN_EP, 0x02, 0x00, 0x02, 0x00,
};

/* MS OS 2.0描述符集(330B=集合头10 + [功能子集CAN 160] + [功能子集DAP 160], 每子集=头8+兼容ID 20+属性132)。
 * 类型值必须按MS规范表9: 集合头0x00/功能子集头0x02/兼容ID 0x03/注册表属性0x04(0x06=ModelID,
 * 0x08=厂商版本)。曾误用0x06/0x08: Windows按ModelID解析10B头但该类型固定22B,
 * 长度不符整集判废, 枚举在BOS后中止(usbccgp报STATUS_NO_SUCH_DEVICE)。 */
static const uint8_t msosv2_descriptor_set[] = {
    0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x06, 0x4A, 0x01, 0x08, 0x00,
    0x02, 0x00, 0x02, 0x00, 0xA0, 0x00, 0x14, 0x00, 0x03, 0x00, 0x57, 0x49,
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
    /* 功能子集2: DAP bulk接口(intf 3)同样绑WinUSB+GUID(330B=集合头10+子集160x2) */
    0x08, 0x00, 0x02, 0x00, 0x03, 0x00, 0xA0, 0x00,
    0x14, 0x00, 0x03, 0x00, 0x57, 0x49, 0x4E, 0x55, 0x53, 0x42, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x84, 0x00, 0x04, 0x00,
    0x07, 0x00, 0x2A, 0x00, 0x44, 0x00, 0x65, 0x00, 0x76, 0x00, 0x69, 0x00,
    0x63, 0x00, 0x65, 0x00, 0x49, 0x00, 0x6E, 0x00, 0x74, 0x00, 0x65, 0x00,
    0x72, 0x00, 0x66, 0x00, 0x61, 0x00, 0x63, 0x00, 0x65, 0x00, 0x47, 0x00,
    0x55, 0x00, 0x49, 0x00, 0x44, 0x00, 0x73, 0x00, 0x00, 0x00, 0x50, 0x00,
    0x7B, 0x00, 0x39, 0x00, 0x32, 0x00, 0x43, 0x00, 0x45, 0x00, 0x36, 0x00,
    0x34, 0x00, 0x36, 0x00, 0x32, 0x00, 0x2D, 0x00, 0x39, 0x00, 0x43, 0x00,
    0x37, 0x00, 0x37, 0x00, 0x2D, 0x00, 0x34, 0x00, 0x36, 0x00, 0x46, 0x00,
    0x45, 0x00, 0x2D, 0x00, 0x39, 0x00, 0x33, 0x00, 0x33, 0x00, 0x42, 0x00,
    0x2D, 0x00, 0x33, 0x00, 0x31, 0x00, 0x43, 0x00, 0x42, 0x00, 0x39, 0x00,
    0x43, 0x00, 0x35, 0x00, 0x41, 0x00, 0x41, 0x00, 0x33, 0x00, 0x42, 0x00,
    0x39, 0x00, 0x7D, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* BOS(33B): MS OS 2.0平台能力, vendor code 0x20。
 * 倒数第4-3字节是 wMSOSDescriptorSetTotalLength, 必须与描述符集内部
 * wTotalLength 严格一致(同一长度两处各存一份, 改集合长度必须两处同改,
 * 不一致Windows直接判废→父设备Error无子接口, 曾漏改踩坑) */
static const uint8_t bos_descriptor[] = {
    0x05, 0x0F, 0x21, 0x00, 0x01, 0x1C, 0x10, 0x05, 0x00, 0xDF, 0x60, 0xDD,
    0xD8, 0x89, 0x45, 0xC7, 0x4C, 0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A,
    0x9F, 0x00, 0x00, 0x03, 0x06, 0x4A, 0x01, 0x20, 0x00,
};

static struct usb_msosv2_descriptor msosv2_desc = {
    .compat_id = msosv2_descriptor_set,
    .compat_id_len = sizeof(msosv2_descriptor_set),   /* 完整描述符集(内部wLength一致) */
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
    "UNICONNECT-19", /* Serial Number: 变更可触发Windows全新安装(重跑MSOS2.0绑定) */
    "CMSIS-DAP", /* 接口字符串: bulk v2识别标志(OpenOCD/pyOCD按此匹配, 规范2.1) */
    "UniConnect CAN Bridge", /* CAN桥接口字符串: 与DAP区分, 防止工具误认 */
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
    if (index > 5) {
        return NULL;
    }
    return string_descriptors[index];
}

const struct usb_descriptor usb_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback,
#if USB_DEBUG_MSOS   /* MSOS2.0: BOS平台能力+描述符集, Windows 8.1+免驱绑定WinUSB */
    .msosv2_descriptor = &msosv2_desc,
    .bos_descriptor = &bos_desc,
#endif
};

static void usbd_event_handler (uint8_t busid, uint8_t event) {
    if (event == USBD_EVENT_CONFIGURED) {
        updateSystemStatus(SYS_STA_USB_CONNECTED);
    }
    /* 事件分发给各功能注册的钩子(数据面在App模块内自愈:
     * CONFIGURED时重建端点/OUT池重挂/挂首包, 总线复位后数据通路不关死) */
    if (cdc_handle.cdc_event_hander_callback) {
        cdc_handle.cdc_event_hander_callback(busid, event);
    }
    if (canusb_handle.cfg_cb) {
        canusb_handle.cfg_cb(busid, event);
    }
    if (dap_handle.cfg_cb) {
        dap_handle.cfg_cb(busid, event);
    }
}

static struct usbd_interface cdc_intf0;
static struct usbd_interface cdc_intf1;
static struct usbd_interface dap_vendor_intf;
static struct usbd_interface can_vendor_intf;

static int bsp_usb_init(void)
{
    usbd_desc_register (0, &usb_descriptor);
#if CANUSB_SOLO_TEST
    /* SOLO: CAN独占接口0 */
    memset(&can_vendor_intf, 0, sizeof(can_vendor_intf));
    usbd_add_interface (0, &can_vendor_intf);
#else
    /* 复合: 注册顺序决定intf_num, 必须与描述符一致 -- CDC先注册拿0/1,
     * DAP再注册拿2(必须先于CAN: OpenOCD取第一个厂商bulk接口, CAN在前会
     * 抢走导致DAP_Info发给CAN桥), CAN最后拿3(SOLO实验时CAN曾前置抢0号,
     * Windows给CDC接口0的类请求被路由到无处理器的CAN接口→STALL→usbser
     * 打开报错31的根因)。端点的addr/回调由各功能在INIT_COMPONENT阶段
     * 填入各自句柄, 此处只做有序挂接 */
    usbd_add_interface (0, usbd_cdc_acm_init_intf (0, &cdc_intf0));
    usbd_add_interface (0, usbd_cdc_acm_init_intf (0, &cdc_intf1));
    usbd_add_endpoint (0, &cdc_handle.out_ep);
    usbd_add_endpoint (0, &cdc_handle.in_ep);
    memset(&dap_vendor_intf, 0, sizeof(dap_vendor_intf));
    usbd_add_interface (0, &dap_vendor_intf);
    usbd_add_endpoint (0, &dap_handle.out_ep);
    usbd_add_endpoint (0, &dap_handle.in_ep);
#endif
    memset(&can_vendor_intf, 0, sizeof(can_vendor_intf));
    usbd_add_interface (0, &can_vendor_intf);
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
