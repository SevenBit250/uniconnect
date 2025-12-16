#include "usb_cdc.h"

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
#define CDC_MAX_MPS 512

/* ringbuffer缓冲区 */
#define USB_RB_RX_BUFFER_SIZE 512u

typedef struct {
    uint8_t cdc_buffer[USB_RB_RX_BUFFER_SIZE];
    uint8_t rb_buffer[USB_RB_RX_BUFFER_SIZE];        // 数据缓冲区 - usage of the thread
    chry_ringbuffer_t rxrb;                          // 缓冲管理器
    struct cdc_line_coding lincoding;                // 行设置
    rt_bool_t is_rx_idle;                            // 是否闲
    rt_bool_t is_tx_idle;                            // 是否闲
    rt_bool_t config_uart;
    rt_bool_t config_uart_transfer;
} usb_cdc_buffer_t;
