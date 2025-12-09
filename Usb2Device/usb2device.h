#ifndef __USB2DEVICE_H
#define __USB2DEVICE_H

#include "ch32v30x.h"

#ifdef __cplusplus
 extern "C" {
#endif

#define UART_DMA_RX_BUFFER_SIZE 512u
#define UART_DMA_TX_BUFFER_SIZE 512u

void uart_perpare(void);

#ifdef __cplusplus
}
#endif

#endif // __USB2UART_H
