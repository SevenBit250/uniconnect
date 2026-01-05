#ifndef __USB_CLASS_H
#define __USB_CLASS_H

#include "usb_class_type.h"

#ifdef __cplusplus
 extern "C" {
#endif

void usb_init(int8_t busid, uintptr_t reg_base);

void cdc_uart_interface_register(int8_t busid);

void cdc_uart_event_handler(uint8_t busid, uint8_t event);

void cdc_uart_acm_bulk_in (uint8_t busid, uint8_t ep, uint32_t nbytes);

void cdc_uart_acm_bulk_out (uint8_t busid, uint8_t ep, uint32_t nbytes);

void winusb_dap_interface_register(int8_t busid);

void winusb_can_interface_register(int8_t busid);

#ifdef __cplusplus
}
#endif

#endif /* __USB_CLASS_H */


