/*
 * Copyright (c) 2022, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "rtthread.h"
#include "usbd_core.h"
#include "usb_ch32_usbhs_reg.h"

#ifndef USB_NUM_BIDIR_ENDPOINTS
#define USB_NUM_BIDIR_ENDPOINTS 16
#endif

#define USB_SET_RX_DMA(ep_idx, addr) (*(volatile uint32_t *)((uint32_t)(&USBHS_DEVICE->UEP1_RX_DMA) + 4 * (ep_idx - 1)) = addr)
#define USB_SET_TX_DMA(ep_idx, addr) (*(volatile uint32_t *)((uint32_t)(&USBHS_DEVICE->UEP1_TX_DMA) + 4 * (ep_idx - 1)) = addr)
#define USB_SET_MAX_LEN(ep_idx, len) (*(volatile uint16_t *)((uint32_t)(&USBHS_DEVICE->UEP0_MAX_LEN) + 4 * ep_idx) = len)
#define USB_SET_TX_LEN(ep_idx, len)  (*(volatile uint16_t *)((uint32_t)(&USBHS_DEVICE->UEP0_TX_LEN) + 4 * ep_idx) = len)
#define USB_GET_TX_LEN(ep_idx)       (*(volatile uint16_t *)((uint32_t)(&USBHS_DEVICE->UEP0_TX_LEN) + 4 * ep_idx))
#define USB_SET_TX_CTRL(ep_idx, val) (*(volatile uint8_t *)((uint32_t)(&USBHS_DEVICE->UEP0_TX_CTRL) + 4 * ep_idx) = val)
#define USB_GET_TX_CTRL(ep_idx)      (*(volatile uint8_t *)((uint32_t)(&USBHS_DEVICE->UEP0_TX_CTRL) + 4 * ep_idx))
#define USB_SET_RX_CTRL(ep_idx, val) (*(volatile uint8_t *)((uint32_t)(&USBHS_DEVICE->UEP0_RX_CTRL) + 4 * ep_idx) = val)
#define USB_GET_RX_CTRL(ep_idx)      (*(volatile uint8_t *)((uint32_t)(&USBHS_DEVICE->UEP0_RX_CTRL) + 4 * ep_idx))

/* Endpoint state */
struct ch32_usbhs_ep_state {
    uint16_t ep_mps;    /* Endpoint max packet size */
    uint8_t ep_type;    /* Endpoint type */
    uint8_t ep_stalled; /* Endpoint stall flag */
    uint8_t ep_enable;  /* Endpoint enable */
    uint8_t *xfer_buf;
    uint32_t xfer_len;
    uint32_t actual_xfer_len;
};

/* Driver state */
struct ch32_usbhs_udc {
    __attribute__((aligned(4))) struct usb_setup_packet setup;
    volatile uint8_t dev_addr;
    struct ch32_usbhs_ep_state in_ep[USB_NUM_BIDIR_ENDPOINTS];  /*!< IN endpoint parameters*/
    struct ch32_usbhs_ep_state out_ep[USB_NUM_BIDIR_ENDPOINTS]; /*!< OUT endpoint parameters */
} g_ch32_usbhs_udc;

volatile uint8_t mps_over_flag = 0;
volatile bool ep0_rx_data_toggle;
volatile bool ep0_tx_data_toggle;
volatile bool epx_tx_data_toggle[USB_NUM_BIDIR_ENDPOINTS - 1];

__WEAK void usb_dc_low_level_init(void)
{
}

__WEAK void usb_dc_low_level_deinit(void)
{
}

int usb_dc_init(uint8_t busid)
{
    usb_dc_low_level_init();

    USBHS_DEVICE->HOST_CTRL = 0x00;
    USBHS_DEVICE->HOST_CTRL = USBHS_PHY_SUSPENDM;

    USBHS_DEVICE->CONTROL = 0;
#ifdef CONFIG_USB_HS
    USBHS_DEVICE->CONTROL = USBHS_DMA_EN | USBHS_INT_BUSY_EN | USBHS_HIGH_SPEED;
#else
    USBHS_DEVICE->CONTROL = USBHS_DMA_EN | USBHS_INT_BUSY_EN | USBHS_FULL_SPEED;
#endif

    USBHS_DEVICE->INT_FG = 0xff;
    USBHS_DEVICE->INT_EN = 0;
    USBHS_DEVICE->INT_EN = USBHS_SETUP_ACT_EN | USBHS_TRANSFER_EN | USBHS_DETECT_EN;

    USBHS_DEVICE->ENDP_TYPE = 0x00;
    USBHS_DEVICE->BUF_MODE = 0x00;

    USBHS_DEVICE->CONTROL |= USBHS_DEV_PU_EN;

    return 0;
}

int usb_dc_deinit(uint8_t busid)
{
    return 0;
}

int usbd_set_address(uint8_t busid, const uint8_t addr)
{
    if (addr == 0) {
        USBHS_DEVICE->DEV_AD = addr & 0xff;
    }
    g_ch32_usbhs_udc.dev_addr = addr;
    return 0;
}

int usbd_set_remote_wakeup(uint8_t busid)
{
    return -1;
}

uint8_t usbd_get_port_speed(uint8_t busid)
{
    return USB_SPEED_HIGH;
}

int usbd_ep_open(uint8_t busid, const struct usb_endpoint_descriptor *ep)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep->bEndpointAddress);

    if (USB_EP_DIR_IS_OUT(ep->bEndpointAddress)) {
        g_ch32_usbhs_udc.out_ep[ep_idx].ep_mps = USB_GET_MAXPACKETSIZE(ep->wMaxPacketSize);
        g_ch32_usbhs_udc.out_ep[ep_idx].ep_type = USB_GET_ENDPOINT_TYPE(ep->bmAttributes);
        g_ch32_usbhs_udc.out_ep[ep_idx].ep_enable = true;
        if (g_ch32_usbhs_udc.out_ep[ep_idx].ep_type == USB_ENDPOINT_TYPE_ISOCHRONOUS) {
            USBHS_DEVICE->ENDP_TYPE |= (1 << (ep_idx + 16));
        } else {
            USBHS_DEVICE->ENDP_TYPE &= ~(1 << (ep_idx + 16));
        }
        USBHS_DEVICE->ENDP_CONFIG |= (1 << (ep_idx + 16));
        USB_SET_RX_CTRL(ep_idx, USBHS_EP_R_RES_NAK | USBHS_EP_R_TOG_0 | USBHS_EP_R_AUTOTOG);
    } else {
        g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps = USB_GET_MAXPACKETSIZE(ep->wMaxPacketSize);
        g_ch32_usbhs_udc.in_ep[ep_idx].ep_type = USB_GET_ENDPOINT_TYPE(ep->bmAttributes);
        g_ch32_usbhs_udc.in_ep[ep_idx].ep_enable = true;
        if (g_ch32_usbhs_udc.in_ep[ep_idx].ep_type == USB_ENDPOINT_TYPE_ISOCHRONOUS) {
            USBHS_DEVICE->ENDP_TYPE |= (1 << (ep_idx));
            USB_SET_TX_CTRL(ep_idx, USBHS_EP_T_RES_NAK | USBHS_EP_T_TOG_0);
        } else {
            USBHS_DEVICE->ENDP_TYPE &= ~(1 << (ep_idx));
            USB_SET_TX_CTRL(ep_idx, USBHS_EP_T_RES_NAK | USBHS_EP_T_TOG_0 | USBHS_EP_T_AUTOTOG);
        }
        USBHS_DEVICE->ENDP_CONFIG |= (1 << (ep_idx));
    }
    USB_SET_MAX_LEN(ep_idx, USB_GET_MAXPACKETSIZE(ep->wMaxPacketSize));
    return 0;
}

int usbd_ep_close(uint8_t busid, const uint8_t ep)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    if (USB_EP_DIR_IS_OUT(ep)) {
        USBHS_DEVICE->ENDP_CONFIG &= ~(1 << (ep_idx + 16));
    } else {
        USBHS_DEVICE->ENDP_CONFIG &= ~(1 << (ep_idx));
    }
    return 0;
}

int usbd_ep_set_stall(uint8_t busid, const uint8_t ep)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);

    if (USB_EP_DIR_IS_OUT(ep)) {
        if (ep_idx == 0) {
            USBHS_DEVICE->UEP0_RX_CTRL = USBHS_EP_R_RES_STALL;
        } else {
            USB_SET_RX_CTRL(ep_idx, (USB_GET_RX_CTRL(ep_idx) & ~USBHS_EP_R_RES_MASK) | USBHS_EP_R_RES_STALL);
        }
    } else {
        if (ep_idx == 0) {
            USBHS_DEVICE->UEP0_TX_CTRL = USBHS_EP_T_RES_STALL;
        } else {
            USB_SET_TX_CTRL(ep_idx, (USB_GET_TX_CTRL(ep_idx) & ~USBHS_EP_T_RES_MASK) | USBHS_EP_T_RES_STALL);
        }
    }

    return 0;
}

int usbd_ep_clear_stall(uint8_t busid, const uint8_t ep)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);

    if (USB_EP_DIR_IS_OUT(ep)) {
        USB_SET_RX_CTRL(ep_idx, USBHS_EP_R_RES_ACK | USBHS_EP_R_TOG_0);
    } else {
        USB_SET_TX_CTRL(ep_idx, USBHS_EP_T_RES_NAK | USBHS_EP_T_TOG_0);
    }
    return 0;
}

int usbd_ep_is_stalled(uint8_t busid, const uint8_t ep, uint8_t *stalled)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);

    if (USB_EP_DIR_IS_OUT(ep)) {
        *stalled = USB_GET_RX_CTRL(ep_idx) & USBHS_EP_R_RES_STALL ? 1 : 0;
    } else {
        *stalled = USB_GET_TX_CTRL(ep_idx) & USBHS_EP_T_RES_STALL ? 1 : 0;
    }
    return 0;
}

int usbd_ep_start_write(uint8_t busid, const uint8_t ep, const uint8_t *data, uint32_t data_len)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    uint32_t tmp;

    if (!data && data_len) {
        return -1;
    }
    if (!g_ch32_usbhs_udc.in_ep[ep_idx].ep_enable) {
        return -2;
    }
    if ((uint32_t)data & 0x03) {
        return -3;
    }

    g_ch32_usbhs_udc.in_ep[ep_idx].xfer_buf = (uint8_t *)data;
    g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len = data_len;
    g_ch32_usbhs_udc.in_ep[ep_idx].actual_xfer_len = 0;

    if (ep_idx == 0) {
        if (data_len == 0) {
            USB_SET_TX_LEN(ep_idx, 0);
        } else {
            data_len = MIN(data_len, g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps);
            USB_SET_TX_LEN(ep_idx, data_len);
            USBHS_DEVICE->UEP0_DMA = (uint32_t)data;
        }
        tmp = ep0_tx_data_toggle ? USBHS_EP_T_TOG_1 : USBHS_EP_T_TOG_0;
        USBHS_DEVICE->UEP0_TX_CTRL = USBHS_EP_T_RES_ACK | tmp;
    } else {
        if (data_len == 0) {
            USB_SET_TX_LEN(ep_idx, 0);
        } else {
            data_len = MIN(data_len, g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps);
            USB_SET_TX_LEN(ep_idx, data_len);
            USB_SET_TX_DMA(ep_idx, (uint32_t)data);
        }
        tmp = USB_GET_TX_CTRL(ep_idx);
        tmp &= ~(USBHS_EP_T_RES_MASK | USBHS_EP_T_TOG_MASK);
        tmp |= USBHS_EP_T_RES_ACK;
        if (g_ch32_usbhs_udc.in_ep[ep_idx].ep_type == USB_ENDPOINT_TYPE_ISOCHRONOUS) {
            tmp |= USBHS_EP_T_TOG_0;
        } else {
            tmp |= (epx_tx_data_toggle[ep_idx - 1] ? USBHS_EP_T_TOG_1 : USBHS_EP_T_TOG_0);
        }
        USB_SET_TX_CTRL(ep_idx, tmp);
    }
    return 0;
}

int usbd_ep_start_read(uint8_t busid, const uint8_t ep, uint8_t *data, uint32_t data_len)
{
    uint8_t ep_idx = USB_EP_GET_IDX(ep);

    if (!data && data_len) {
        return -1;
    }
    if (!g_ch32_usbhs_udc.out_ep[ep_idx].ep_enable) {
        return -2;
    }
    if ((uint32_t)data & 0x03) {
        return -3;
    }

    g_ch32_usbhs_udc.out_ep[ep_idx].xfer_buf = (uint8_t *)data;
    g_ch32_usbhs_udc.out_ep[ep_idx].xfer_len = data_len;
    g_ch32_usbhs_udc.out_ep[ep_idx].actual_xfer_len = 0;

    if (ep_idx == 0) {
        if (data_len == 0) {
            USBHS_DEVICE->UEP0_RX_CTRL = USBHS_EP_R_RES_ACK | USBHS_EP_R_TOG_1;
        } else {
            USBHS_DEVICE->UEP0_DMA = (uint32_t)data;
            USBHS_DEVICE->UEP0_RX_CTRL = USBHS_EP_R_RES_ACK | (ep0_rx_data_toggle ? USBHS_EP_R_TOG_1 : USBHS_EP_R_TOG_0);
        }
        return 0;
    } else {
        USB_SET_RX_DMA(ep_idx, (uint32_t)data);
        if (g_ch32_usbhs_udc.out_ep[ep_idx].ep_type == USB_ENDPOINT_TYPE_ISOCHRONOUS) {
            USB_SET_RX_CTRL(ep_idx, (USB_GET_RX_CTRL(ep_idx) & ~(USBHS_EP_R_RES_MASK | USBHS_EP_R_TOG_MASK)) | USBHS_EP_R_RES_ACK | USBHS_EP_R_TOG_0);
        } else {
            USB_SET_RX_CTRL(ep_idx, (USB_GET_RX_CTRL(ep_idx) & ~USBHS_EP_R_RES_MASK) | USBHS_EP_R_RES_ACK);
        }
    }

    return 0;
}

void USBD_IRQHandler(uint8_t busid)
{
    uint32_t ep_idx, token, write_count, read_count;
    uint8_t intflag = 0;

    intflag = USBHS_DEVICE->INT_FG;

    /* 全标志闭环处理: PFIC对"ISR运行期间置位"的标志不会可靠重触发(5M丢块
     * 已证TRANSFER如此), SETUP同理 -- 外层重查堵住transfer循环尾读INT_FG后
     * 才到达的SETUP, 否则该SETUP永远无人处理, usbccgp的SET_CONFIGURATION
     * 超时(实测4s), 复合设备以STATUS_NO_SUCH_DEVICE启动失败(WinUSB绑定之障) */
    do {
    /* TRANSFER标志保留至循环体末尾(端点重武装之后)才清除:
     * RM RB_UC_INT_BUSY(CTRL bit3)语义为标志置位期间SIE自动NAK所有新事务,
     * 若在入口提前清除, 处理当前包期间到达的下一个OUT包会被ACK进尚未换出的
     * 旧RX缓冲, 覆写正在消费的数据(HS背靠背包4M/5M丢块根因, FS包间隔大不触发)。
     * 标志清除后主机PING重试的新事务由循环重查继续处理 */
    while (intflag & USBHS_TRANSFER_FLAG) {
        uint32_t int_st = USBHS_DEVICE->INT_ST; /* 快照: 处理期间NAK重试可能刷新INT_ST */
        ep_idx = int_st & MASK_UIS_ENDP;
        token = ((int_st & MASK_UIS_TOKEN) >> 4) & 0x03;

        if (token == PID_IN) {
            USB_SET_TX_CTRL(ep_idx, (USB_GET_TX_CTRL(ep_idx) & ~(USBHS_EP_T_RES_MASK | USBHS_EP_T_TOG_MASK)) | USBHS_EP_T_RES_NAK | USBHS_EP_T_TOG_0);
            if (ep_idx == 0x00) {
                /* EP0多包IN: 完成回调必须传"本包"字节数而非累计值 --
                 * 核心层按nbytes递减residue并前移缓冲, 传累计值会使第2包后
                 * residue下溢/指针错位, 第3包起发出错位数据(>128B控制IN全灭,
                 * 如MSOS2.0描述符集166B; <=2包侥幸无害故此前未暴露) */
                uint32_t pkt_bytes;
                if (g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len >= g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps) {
                    pkt_bytes = g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps;
                    g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len -= g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps;
                    g_ch32_usbhs_udc.in_ep[ep_idx].actual_xfer_len += g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps;
                    ep0_tx_data_toggle ^= 1;
                } else {
                    pkt_bytes = g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len;
                    g_ch32_usbhs_udc.in_ep[ep_idx].actual_xfer_len += g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len;
                    g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len = 0;
                    ep0_tx_data_toggle = true;
                }

                usbd_event_ep_in_complete_handler(0, ep_idx | 0x80, pkt_bytes);

                if (g_ch32_usbhs_udc.dev_addr > 0) {
                    USBHS_DEVICE->DEV_AD = g_ch32_usbhs_udc.dev_addr & 0xff;
                    g_ch32_usbhs_udc.dev_addr = 0;
                }

                if (g_ch32_usbhs_udc.setup.wLength && ((g_ch32_usbhs_udc.setup.bmRequestType & USB_REQUEST_DIR_MASK) == USB_REQUEST_DIR_OUT)) {
                    /* In status, start reading setup */
                    USBHS_DEVICE->UEP0_DMA = (uint32_t)&g_ch32_usbhs_udc.setup;
                    USBHS_DEVICE->UEP0_RX_CTRL = USBHS_EP_R_RES_ACK;
                    ep0_tx_data_toggle = true;

                } else if (g_ch32_usbhs_udc.setup.wLength == 0) {
                    /* In status, start reading setup */
                    USBHS_DEVICE->UEP0_DMA = (uint32_t)&g_ch32_usbhs_udc.setup;
                    USBHS_DEVICE->UEP0_RX_CTRL = USBHS_EP_R_RES_ACK;
                    ep0_tx_data_toggle = true;
                }
            } else {
                /* off-by-one修复: 原条件为'>', 剩余恰为一包(==mps)时误走完成分支,
                 * 尾包被计入actual却从未发出(合并大传输时丢512B的根因) */
                if (g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len >= g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps) {
                    g_ch32_usbhs_udc.in_ep[ep_idx].xfer_buf += g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps;
                    g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len -= g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps;
                    g_ch32_usbhs_udc.in_ep[ep_idx].actual_xfer_len += g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps;
                    epx_tx_data_toggle[ep_idx - 1] ^= 1;

                    write_count = MIN(g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len, g_ch32_usbhs_udc.in_ep[ep_idx].ep_mps);
                    USB_SET_TX_LEN(ep_idx, write_count);
                    USB_SET_TX_DMA(ep_idx, (uint32_t)g_ch32_usbhs_udc.in_ep[ep_idx].xfer_buf);

                    uint32_t tmp = USB_GET_TX_CTRL(ep_idx);
                    tmp &= ~(USBHS_EP_T_RES_MASK | USBHS_EP_T_TOG_MASK);
                    tmp |= USBHS_EP_T_RES_ACK;

                    if (g_ch32_usbhs_udc.in_ep[ep_idx].ep_type == USB_ENDPOINT_TYPE_ISOCHRONOUS) {
                        tmp |= USBHS_EP_T_TOG_0;
                    } else {
                        tmp |= (epx_tx_data_toggle[ep_idx - 1] ? USBHS_EP_T_TOG_1 : USBHS_EP_T_TOG_0);
                    }

                    USB_SET_TX_CTRL(ep_idx, tmp);
                } else {
                    g_ch32_usbhs_udc.in_ep[ep_idx].actual_xfer_len += g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len;
                    g_ch32_usbhs_udc.in_ep[ep_idx].xfer_len = 0;
                    epx_tx_data_toggle[ep_idx - 1] ^= 1;
                    usbd_event_ep_in_complete_handler(0, ep_idx | 0x80, g_ch32_usbhs_udc.in_ep[ep_idx].actual_xfer_len);
                }
            }
        } else if (token == PID_OUT) {
            USB_SET_RX_CTRL(ep_idx, (USB_GET_RX_CTRL(ep_idx) & ~USBHS_EP_R_RES_MASK) | USBHS_EP_R_RES_NAK);
            if (ep_idx == 0x00) {
                read_count = USBHS_DEVICE->RX_LEN;

                g_ch32_usbhs_udc.out_ep[ep_idx].actual_xfer_len += read_count;
                g_ch32_usbhs_udc.out_ep[ep_idx].xfer_len -= read_count;

                usbd_event_ep_out_complete_handler(0, 0x00, g_ch32_usbhs_udc.out_ep[ep_idx].actual_xfer_len);

                if (read_count == 0) {
                    /* Out status, start reading setup */
                    USBHS_DEVICE->UEP0_DMA = (uint32_t)&g_ch32_usbhs_udc.setup;
                    USBHS_DEVICE->UEP0_RX_CTRL = USBHS_EP_R_RES_ACK;
                    ep0_rx_data_toggle = true;
                    ep0_tx_data_toggle = true;
                } else {
                    ep0_rx_data_toggle ^= 1;
                }
            } else {
                if (ep_idx == 0x02) {
                    extern volatile uint32_t g_dap_dbg[4];
                    g_dap_dbg[3]++;   /* EP2(DAP OUT)事务到达ISR计数 */
                }
                if (!(int_st & USBHS_DEV_UIS_TOG_OK)) {
                    extern volatile uint32_t g_out_tog_drop;
                    g_out_tog_drop++;   /* TOG_OK失败被丢弃的包(已ACK, 数据黑洞) */
                }
                if ((g_ch32_usbhs_udc.out_ep[ep_idx].xfer_buf != NULL) &&
                    (g_ch32_usbhs_udc.out_ep[ep_idx].xfer_len != 0) &&
                    (int_st & USBHS_DEV_UIS_TOG_OK)) {
                    /* 守卫: 总线复位会memset端点状态, 挂起的陈旧OUT完成事件若继续
                     * 处理, xfer_len下溢成巨值会走续传分支, 把RX_DMA写成垃圾地址,
                     * USB OUT DMA随即把数据包写入任意RAM(实测涂烂黑匣子/TCB区) */
                    read_count = USBHS_DEVICE->RX_LEN;
                    if (read_count > g_ch32_usbhs_udc.out_ep[ep_idx].xfer_len) {
                        read_count = g_ch32_usbhs_udc.out_ep[ep_idx].xfer_len;
                    }

                    g_ch32_usbhs_udc.out_ep[ep_idx].xfer_buf += read_count;
                    g_ch32_usbhs_udc.out_ep[ep_idx].actual_xfer_len += read_count;
                    g_ch32_usbhs_udc.out_ep[ep_idx].xfer_len -= read_count;

                    if ((read_count < g_ch32_usbhs_udc.out_ep[ep_idx].ep_mps) || (g_ch32_usbhs_udc.out_ep[ep_idx].xfer_len == 0)) {
                        /* 完成回调传"本次"字节数而非累计值: 多包传输时累计值会让
                         * 消费者把块长记成滚雪球的错值(CAN OUT池曾录得771B的根源) */
                        usbd_event_ep_out_complete_handler(0, ep_idx, read_count);
                    } else {
                        USB_SET_RX_DMA(ep_idx, (uint32_t)g_ch32_usbhs_udc.out_ep[ep_idx].xfer_buf);
                        USB_SET_RX_CTRL(ep_idx, (USB_GET_RX_CTRL(ep_idx) & ~USBHS_EP_R_RES_MASK) | USBHS_EP_R_RES_ACK);
                    }
                }
            }
        }
        /* 端点重武装完成后才清标志(INT_BUSY保护ISR全程), 循环重查新事务 */
        USBHS_DEVICE->INT_FG = USBHS_TRANSFER_FLAG;
        intflag = USBHS_DEVICE->INT_FG;
    }

    if (intflag & USBHS_SETUP_FLAG) {
        USBHS_DEVICE->INT_FG = USBHS_SETUP_FLAG;
        usbd_event_ep0_setup_complete_handler(0, (uint8_t *)&g_ch32_usbhs_udc.setup);
    }
    intflag = USBHS_DEVICE->INT_FG;   /* 尾部重查: 循环尾读后才到的标志不再漏 */
    } while (intflag & (USBHS_TRANSFER_FLAG | USBHS_SETUP_FLAG));

    if (intflag & USBHS_DETECT_FLAG) {
        USBHS_DEVICE->INT_FG = USBHS_DETECT_FLAG;

        USBHS_DEVICE->ENDP_CONFIG = USBHS_EP0_T_EN | USBHS_EP0_R_EN;

        USBHS_DEVICE->UEP0_TX_LEN = 0;
        USBHS_DEVICE->UEP0_TX_CTRL = USBHS_EP_T_RES_NAK;

        ep0_tx_data_toggle = true;
        ep0_rx_data_toggle = true;

        for (uint8_t ep_idx = 1; ep_idx < USB_NUM_BIDIR_ENDPOINTS; ep_idx++) {
            USB_SET_TX_LEN(ep_idx, 0);
            USB_SET_TX_CTRL(ep_idx, USBHS_EP_T_AUTOTOG | USBHS_EP_T_RES_NAK); // autotog does not work
            USB_SET_RX_CTRL(ep_idx, USBHS_EP_R_AUTOTOG | USBHS_EP_R_RES_NAK);
            epx_tx_data_toggle[ep_idx - 1] = false;
        }

        memset(&g_ch32_usbhs_udc, 0, sizeof(struct ch32_usbhs_udc));
        usbd_event_reset_handler(0);
        USBHS_DEVICE->UEP0_DMA = (uint32_t)&g_ch32_usbhs_udc.setup;
        USBHS_DEVICE->UEP0_RX_CTRL = USBHS_EP_R_RES_ACK;
    }
}

void USBHS_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USBHS_IRQHandler(void)
{
    extern void USBD_IRQHandler(uint8_t busid);
    rt_interrupt_enter();
    USBD_IRQHandler(0);
    rt_interrupt_leave();
}
