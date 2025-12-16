#ifndef __USB2DEVICE_H
#define __USB2DEVICE_H

#include "ch32v30x.h"
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 函数声明 */
void uart_perpare (void);

void uart_config_override(uint32_t baudrate, uint8_t stopbits, uint8_t parity);

rt_bool_t uart_write_to_usb_ready(void);

uint8_t* uart_write_to_usb_setup (uint32_t *size);

void uart_write_to_usb_done (uint32_t size);

void uart_write_to_dev(const uint8_t *data, uint32_t size);

void uart_writeln_to_dev(const char *str);

/* week define */
void uart_rcc_config_callback (void);



#ifdef __cplusplus
}
#endif

#endif  // __USB2DEVICE_H
