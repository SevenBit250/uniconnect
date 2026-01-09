/*!< endpoint address */
#define CDC_IN_EP          0x81
#define CDC_OUT_EP         0x02
#define CDC_INT_EP         0x83

#define USBD_VID           0x0D28
#define USBD_PID           0x0204
#define USBD_MAX_POWER     500
#define USBD_LANGID_STRING 1033

/*!< config descriptor size */
#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN)

/*!< mps usbfs: 64 */
#define CDC_MAX_MPS 512u

#define USB_CDC_BUFFER_SIZE (CDC_MAX_MPS)

/* ringbuffer缓冲区 */
#define USB_RB_RX_BUFFER_SIZE (USB_CDC_BUFFER_SIZE * 2)