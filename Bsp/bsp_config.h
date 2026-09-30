#ifndef __BSP_CONFIG_H
#define __BSP_CONFIG_H

/*!< endpoint address */
#define DAP_IN_EP          0x81
#define DAP_OUT_EP         0x02

#define CDC_IN_EP          0x82
#define CDC_OUT_EP         0x04
#define CDC_INT_EP         0x85

#define USBD_VID           0x0D28
#define USBD_PID           0x0218
#define USBD_MAX_POWER     500
#define USBD_LANGID_STRING 1033

/*!< mps usbfs: 64 */
#define CDC_MAX_MPS 512u

/* cdc out(usb->uart) 块缓冲池: USB OUT直接落块, 零拷贝交接UART DMA (块首恒4字节对齐) */
#define USB_CDC_OUT_BLOCK_SIZE  512u   /* = USB HS bulk MPS */
#define USB_CDC_OUT_BLOCK_COUNT 8u     /* 池深 8*512=4KB */


/* uart rx 环形缓冲: 循环DMA落环, 满块贪心收割零拷贝交接USBHS DMA
 * (块首恒4字节对齐)。深度=抖动容忍: 8KB@921600≈89ms, 主机读轮询抖动
 * 远小于此 => 活跃读取下零丢失; 仅主机彻底停读时回绕丢弃(真无流控边界) */
#define UART_RX_BLOCK_SIZE   512u   /* = USB HS bulk MPS */
#define UART_RX_RING_SIZE    8192u  /* 16块, 必须是2的幂且为块尺寸整数倍 */

/* ---- CAN ----
 * CH32V30x CAN为bxCAN架构(邮箱/FIFO), 无DMA请求线(RM第24章全文无DMA) --
 * 用FIFO0 FMP中断+帧环实现, 每帧ISR开销4次寄存器读+拷贝16B,
 * 1M波特率满载(~8k帧/s)时ISR占用<2% CPU, 非瓶颈。
 * 引脚: CAN1=PB8/PB9(Remap1, PD0/PD1在48脚封装不存在), CAN2=PB12/PB13(固定) */
#define CAN_BITRATE_KBPS     1000u  /* 目标波特率(回环自测+未来总线) */
#define CAN_TX_RING_LEN      28u    /* 每通道发送帧环深度: USB全速灌批时1M波特率清环需2.6ms/20帧, 16深溢出(实测每20帧丢2) */
#define CAN_RX_RING_LEN      16u    /* 每通道接收帧环深度 */

/* CAN桥USB接口(WinUSB厂商bulk, 单接口双路复用) */
#define CAN_IF_IN_EP         0x83
#define CAN_IF_OUT_EP        0x06
#define CAN_USB_BLOCK_SIZE   512u   /* = USB HS bulk MPS */
#define CAN_USB_OUT_BLOCKS   4u     /* OUT(usb->can)块池深 */
#define CAN_USB_IN_BLOCKS    2u     /* IN(can->usb)拼装块: 在途1+拼装1轮转 */

#endif