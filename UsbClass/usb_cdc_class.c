/*
 * Copyright (c) 2024, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "usb2device.h"
#include "chry_ringbuffer.h"
#include "usb_device_class.h"
#include "usb_device_class_private.h"
#include "rtthread.h"

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT (USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01)};

static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT (USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT (0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, CDC_MAX_MPS, 0x02)};

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

const struct usb_descriptor cdc_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback
};

static usb_cdc_buffer_t g_usb_cdc;

static void usbd_event_handler (uint8_t busid, uint8_t event) {
    switch (event) {
    case USBD_EVENT_RESET:
        g_usb_cdc.is_rx_idle = RT_FALSE;
        g_usb_cdc.is_tx_idle = RT_FALSE;
        g_usb_cdc.config_uart = RT_FALSE;
        g_usb_cdc.config_uart_transfer = RT_FALSE;
        break;
    case USBD_EVENT_CONNECTED:
        break;
    case USBD_EVENT_DISCONNECTED:
        break;
    case USBD_EVENT_RESUME:
        break;
    case USBD_EVENT_SUSPEND:
        break;
    case USBD_EVENT_CONFIGURED:
        /* setup first out ep read transfer */
        usbd_ep_start_read(0, CDC_OUT_EP, g_usb_cdc.cdc_buffer, sizeof(g_usb_cdc.cdc_buffer));
        break;
    case USBD_EVENT_SET_REMOTE_WAKEUP:
        break;
    case USBD_EVENT_CLR_REMOTE_WAKEUP:
        break;

    default:
        break;
    }
}

void usbd_cdc_acm_bulk_out (uint8_t busid, uint8_t ep, uint32_t nbytes) 
{
    (void)busid;
    chry_ringbuffer_write(&g_usb_cdc.rxrb, g_usb_cdc.cdc_buffer, nbytes);
    if (chry_ringbuffer_get_free(&g_usb_cdc.rxrb) >= sizeof(g_usb_cdc.cdc_buffer)) {
        usbd_ep_start_read(0, CDC_OUT_EP, g_usb_cdc.cdc_buffer, sizeof(g_usb_cdc.cdc_buffer));
    } else {
        g_usb_cdc.is_rx_idle = RT_TRUE;
    }
}

void usbd_cdc_acm_bulk_in (uint8_t busid, uint8_t ep, uint32_t nbytes) 
{
    (void)busid;
    uart_write_to_usb_done (nbytes);
    if ((nbytes % usbd_get_ep_mps (busid, ep)) == 0 && nbytes) {
        /* send zlp */
        usbd_ep_start_write (busid, CDC_IN_EP, NULL, 0);
    } else {
        if (uart_write_to_usb_ready()) {
            uint32_t size;
            uint8_t *buffer = uart_write_to_usb_setup(&size);
            usbd_ep_start_write(0, CDC_IN_EP, buffer, size);
        } else {
            g_usb_cdc.is_tx_idle = RT_TRUE;
        }
    }
}

/*!< endpoint call back */
struct usbd_endpoint cdc_out_ep = {
    .ep_addr = CDC_OUT_EP,
    .ep_cb = usbd_cdc_acm_bulk_out};

struct usbd_endpoint cdc_in_ep = {
    .ep_addr = CDC_IN_EP,
    .ep_cb = usbd_cdc_acm_bulk_in};

static struct usbd_interface intf0;
static struct usbd_interface intf1;

void usbd_cdc_acm_set_line_coding (uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding) {
    (void)busid;
    if (memcmp (line_coding, (uint8_t *)&g_usb_cdc.lincoding, sizeof (struct cdc_line_coding)) != 0) {
        memcpy ((uint8_t *)&g_usb_cdc.lincoding, line_coding, sizeof (struct cdc_line_coding));
        g_usb_cdc.config_uart = RT_TRUE;
        g_usb_cdc.config_uart_transfer = RT_FALSE;
    }
}

void usbd_cdc_acm_get_line_coding (uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding) {
    (void)busid;
    memcpy (line_coding, (uint8_t *)&g_usb_cdc.lincoding, sizeof (struct cdc_line_coding));
}

void cdc_acm_init (uint8_t busid, uintptr_t reg_base) 
{
    chry_ringbuffer_init(&g_usb_cdc.rxrb, g_usb_cdc.rb_buffer, sizeof(g_usb_cdc.rb_buffer));

    usbd_desc_register (busid, &cdc_descriptor);
    usbd_add_interface (busid, usbd_cdc_acm_init_intf (busid, &intf0));
    usbd_add_interface (busid, usbd_cdc_acm_init_intf (busid, &intf1));
    usbd_add_endpoint (busid, &cdc_out_ep);
    usbd_add_endpoint (busid, &cdc_in_ep);
    usbd_initialize (busid, reg_base, usbd_event_handler);

    uart_perpare();
}

void cdc_acm_handle (void) 
{
    uint32_t size;
    uint8_t *buffer;

    if (g_usb_cdc.config_uart) {
        /* disable irq here */
        g_usb_cdc.config_uart = RT_FALSE;
        /* config uart here */
        uart_config_override (g_usb_cdc.lincoding.dwDTERate, g_usb_cdc.lincoding.bCharFormat, g_usb_cdc.lincoding.bParityType);
        g_usb_cdc.is_tx_idle = RT_TRUE;
        g_usb_cdc.config_uart_transfer = RT_TRUE;
        /* enable irq here */
        rt_kprintf("code lincoding....%d %d %d\r\n", g_usb_cdc.lincoding.dwDTERate, g_usb_cdc.lincoding.bCharFormat, g_usb_cdc.lincoding.bParityType);
    }

    if (g_usb_cdc.config_uart_transfer == RT_FALSE) {
        return;
    }

    /* uartrx to usb tx */
    if (g_usb_cdc.is_tx_idle) {
        if (uart_write_to_usb_ready()) {
            g_usb_cdc.is_tx_idle = RT_FALSE;
            /* start first transfer */
            buffer = uart_write_to_usb_setup (&size);
            usbd_ep_start_write (0, CDC_IN_EP, buffer, size);
            
        }
    }

    /* usbrx to uart tx */
    if (chry_ringbuffer_get_used(&g_usb_cdc.rxrb)) {
        /* start first transfer */
        buffer = chry_ringbuffer_linear_read_setup(&g_usb_cdc.rxrb, &size);
        uart_write_to_dev(buffer, size);
        chry_ringbuffer_linear_read_done(&g_usb_cdc.rxrb, size);
    }

    /* check whether usb rx ringbuffer have space to store */
    if (g_usb_cdc.is_rx_idle) {
        if (chry_ringbuffer_get_free(&g_usb_cdc.rxrb) >= sizeof(g_usb_cdc.cdc_buffer)) {
            g_usb_cdc.is_rx_idle = RT_FALSE;
            usbd_ep_start_read(0, CDC_OUT_EP, g_usb_cdc.cdc_buffer, sizeof(g_usb_cdc.cdc_buffer));
        }
    }
}