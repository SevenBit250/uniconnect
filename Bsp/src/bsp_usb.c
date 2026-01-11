#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "bsp_usb.h"
#include "bsp_config.h"
#include "main.h"

/*!< config descriptor size */
#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN)

bsp_usb_cdc_buffer_t cdc_handle;
static struct usbd_interface cdc_intf0;
static struct usbd_interface cdc_intf1;
static __attribute__((aligned(4))) uint8_t cdc_rx_buffer[USB_CDC_BUFFER_SIZE];
static __attribute__((aligned(4))) uint8_t rb_cdc_rx_buffer[USB_RB_CDC_RX_BUFFER_SIZE];

int bsp_usb_cdc_ep_start_read(uint8_t busid) {
    return usbd_ep_start_read(busid, CDC_OUT_EP, cdc_rx_buffer, sizeof(cdc_rx_buffer));
}

int bsp_usb_cdc_ep_start_write(uint8_t busid, const uint8_t *data, uint32_t data_len) {
    return usbd_ep_start_write(busid, CDC_IN_EP, data, data_len);
}

uint8_t* bsp_usb_cdc_get_buffer(void)
{
    return cdc_rx_buffer;
}

int bsp_usb_cdc_get_buffer_size(void)
{
    return sizeof(cdc_rx_buffer);
}

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
    chry_ringbuffer_init(&cdc_handle.rb_cdc_rx, rb_cdc_rx_buffer, sizeof(rb_cdc_rx_buffer));
    return 0;
}
INIT_COMPONENT_EXPORT(bsp_usb_env_init);

static int bsp_usb_init(void)
{
    usbd_desc_register (0, &usb_descriptor);
    /* cdc config */
    cdc_handle.in_ep.ep_addr = CDC_IN_EP;
    cdc_handle.out_ep.ep_addr = CDC_OUT_EP;
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
    RCC_USBCLK48MConfig(RCC_USBCLK48MCLKSource_USBPHY);
    RCC_USBHSPLLCLKConfig(RCC_HSBHSPLLCLKSource_HSE);
    RCC_USBHSConfig(RCC_USBPLL_Div2);
    RCC_USBHSPLLCKREFCLKConfig( RCC_USBHSPLLCKREFCLK_4M);
    RCC_USBHSPHYPLLALIVEcmd(ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, ENABLE);
    NVIC_EnableIRQ(USBHS_IRQn);
}