#ifndef __BSP_CONFIG_H
#define __BSP_CONFIG_H

/*!< endpoint address */
#define DAP_IN_EP          0x81
#define DAP_OUT_EP         0x02

#define CDC_IN_EP          0x82
#define CDC_OUT_EP         0x04
#define CDC_INT_EP         0x85

#define USBD_VID           0x0D28
#define USBD_PID           0x0204
#define USBD_MAX_POWER     500
#define USBD_LANGID_STRING 1033

/*!< mps usbfs: 64 */
#define CDC_MAX_MPS 512u

/* cdc out(usb->uart) 块缓冲池: USB OUT直接落块, 零拷贝交接UART DMA (块首恒4字节对齐) */
#define USB_CDC_OUT_BLOCK_SIZE  512u   /* = USB HS bulk MPS */
#define USB_CDC_OUT_BLOCK_COUNT 8u     /* 池深 8*512=4KB */


/* uart rx 块缓冲池: DMA直接落块, 零拷贝交接USBHS DMA (块首恒4字节对齐) */
#define UART_RX_BLOCK_SIZE   512u   /* = USB HS bulk MPS */
#define UART_RX_BLOCK_COUNT  8u     /* 池深 8*512=4KB */

#endif