#ifndef __BSP_CONFIG_H
#define __BSP_CONFIG_H

/*!< endpoint address */
#define CDC_IN_EP          0x81
#define CDC_OUT_EP         0x02
#define CDC_INT_EP         0x83

#define USBD_VID           0x0D28
#define USBD_PID           0x0204
#define USBD_MAX_POWER     500
#define USBD_LANGID_STRING 1033

/*!< mps usbfs: 64 */
#define CDC_MAX_MPS 512u
/* cdc rx 缓冲长度 */
#define USB_CDC_BUFFER_SIZE (CDC_MAX_MPS)
/* cdc rx to uart 缓冲长度 */
#define USB_RB_CDC_RX_BUFFER_SIZE (USB_CDC_BUFFER_SIZE * 2)


/* uart rx to cdc 缓冲区长度 */
#define UART_RB_RX_BUFFER_SIZE 1024u
/* uart dma rx 缓冲区长度 */
#define UART_DMA_RX_BUFFER_SIZE   512u

#endif