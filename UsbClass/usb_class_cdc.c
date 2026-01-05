/*
 * Copyright (c) 2024, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "usb_class.h"

static struct usbd_interface intf0;
static struct usbd_interface intf1;

/*!< endpoint call back */
struct usbd_endpoint cdc_out_ep = {
    .ep_addr = CDC_OUT_EP,
    .ep_cb = cdc_uart_acm_bulk_out
};

struct usbd_endpoint cdc_in_ep = {
    .ep_addr = CDC_IN_EP,
    .ep_cb = cdc_uart_acm_bulk_in
};

__WEAK void cdc_uart_acm_bulk_in (uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    /* do nothing */
}

__WEAK void cdc_uart_acm_bulk_out (uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    /* do nothing */
}

void cdc_uart_interface_register (int8_t busid) 
{
    usbd_add_interface (busid, usbd_cdc_acm_init_intf (busid, &intf0));
    usbd_add_interface (busid, usbd_cdc_acm_init_intf (busid, &intf1));
    usbd_add_endpoint (busid, &cdc_out_ep);
    usbd_add_endpoint (busid, &cdc_in_ep);
}