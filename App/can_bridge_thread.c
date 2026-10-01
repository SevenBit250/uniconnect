#include <rtthread.h>
#include <rthw.h>
#include <string.h>
#include "config.h"
#include "bsp_config.h"
#include "bsp_can.h"
#include "bsp_usb.h"
#include "bsp_pool.h"
#include "usbd_core.h"

/* CAN-USB桥接线程: WinUSB bulk单接口双路复用
 *
 * OUT(usb->can): 512B块池 -> 解析CANP_TX/CANP_CTRL包 -> bsp_can_send/执行
 * IN(can->usb): 帧环+回执环+事件 -> 拼装512B块(4对齐) -> bulk IN链式发送
 *   满512整数倍后必须先发ZLP再发下一块(主机按短包判定传输边界);
 *   无在途块且有数据即发(不等块满), 兼顾吞吐与交互延迟
 * 任何路径塞不下即丢弃(seq可检测), 绝不阻塞中断/线程 */

static struct rt_thread can_bridge_thread;
ALIGN(RT_ALIGN_SIZE) static char can_bridge_stack[CAN_BRIDGE_THREAD_STACK_SIZE];

/***************************************************************************/
/*                                                                         */
/*              can-usb桥 out(usb->can)块缓冲池数据面 (2026-10-01          */
/*              由bsp_usb.c移入: USB落块, 桥接线程pop解析, 池尽NAK背压)     */
/*                                                                         */
/***************************************************************************/

/* out池: 块首恒4字节对齐, 直接作为OUT端点DMA落点 */
static __attribute__((aligned(4))) uint8_t canusb_out_pool_storage[CAN_USB_BLOCK_SIZE * CAN_USB_OUT_BLOCKS];
static volatile uint8_t *canusb_out_landing = RT_NULL;
static volatile uint8_t canusb_out_paused = 0;

static void can_out_arm(uint8_t *block)
{
    canusb_out_landing = block;
    usbd_ep_start_read(0, CAN_IF_OUT_EP, block, CAN_USB_BLOCK_SIZE);
}

/* CAN桥端点描述符(厂商bulk接口无类驱动, 自备; wMaxPacketSize=CAN_USB_BLOCK_SIZE=512) */
static struct usb_endpoint_descriptor can_ep_out_desc = {
    .bLength = 0x07,
    .bDescriptorType = USB_DESCRIPTOR_TYPE_ENDPOINT,
    .bEndpointAddress = CAN_IF_OUT_EP,
    .bmAttributes = USB_ENDPOINT_TYPE_BULK,
    .wMaxPacketSize = CAN_USB_BLOCK_SIZE,
    .bInterval = 0,
};
static struct usb_endpoint_descriptor can_ep_in_desc = {
    .bLength = 0x07,
    .bDescriptorType = USB_DESCRIPTOR_TYPE_ENDPOINT,
    .bEndpointAddress = CAN_IF_IN_EP,
    .bmAttributes = USB_ENDPOINT_TYPE_BULK,
    .wMaxPacketSize = CAN_USB_BLOCK_SIZE,
    .bInterval = 0,
};

static void can_out_done(uint16_t nbytes)
{
    if (nbytes == 0) {
        /* 主机ZLP: 原块重挂 */
        can_out_arm((uint8_t *)canusb_out_landing);
        return;
    }
    bsp_pool_submit(&canusb_handle.out_pool, (uint8_t *)canusb_out_landing, nbytes);
    uint8_t *next = bsp_pool_acquire(&canusb_handle.out_pool);
    if (next != RT_NULL) {
        can_out_arm(next);
    } else {
        canusb_out_paused = 1;
    }
    /* 唤醒统一走can活动信号量(桥接线程唯一等待点) */
    rt_sem_release(bsp_can_activity_sem());
}

static void can_out_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    can_out_done((uint16_t)nbytes);
}

static int can_in_start_write(uint8_t busid, const uint8_t *data, uint32_t data_len)
{
    return usbd_ep_start_write(busid, CAN_IF_IN_EP, data, data_len);
}

static uint8_t can_out_pop(uint8_t **block, uint16_t *len)
{
    return bsp_pool_pop(&canusb_handle.out_pool, block, len);
}

static void can_out_recycle(uint8_t *block)
{
    bsp_pool_recycle(&canusb_handle.out_pool, block);
}

static void can_out_try_resume(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (canusb_out_paused) {
        uint8_t *next = bsp_pool_acquire(&canusb_handle.out_pool);
        if (next != RT_NULL) {
            can_out_arm(next);
            canusb_out_paused = 0;
        }
    }
    rt_hw_interrupt_enable(level);
}

/* 重建OUT池并重挂端点(CONFIGURED/总线复位时调用) */
static void can_out_reset(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    bsp_pool_reset(&canusb_handle.out_pool, RT_NULL, 0);
    uint8_t *first = bsp_pool_acquire(&canusb_handle.out_pool);
    if (first != RT_NULL) {
        can_out_arm(first);
        canusb_out_paused = 0;
    } else {
        canusb_out_paused = 1;
    }
    rt_hw_interrupt_enable(level);
}

/* 事件钩子(CONFIGURED时重开端点+重挂, 幂等) */
static void can_usb_cfg_cb(uint8_t busid, uint8_t event)
{
    (void)busid;
    if (event == USBD_EVENT_CONFIGURED) {
        usbd_ep_open(0, &can_ep_out_desc);
        usbd_ep_open(0, &can_ep_in_desc);
        can_out_reset();
    }
}

/* IN拼装块池: 轮转, 在途<=1块 */
static __attribute__((aligned(4))) uint8_t in_blocks[CAN_USB_IN_BLOCKS][CAN_USB_BLOCK_SIZE];
static uint8_t in_cur = 0;
static uint16_t in_len = 0;
static volatile uint8_t in_flight = 0;
static volatile uint8_t zlp_pending = 0;
static uint16_t dev_seq = 0;
static uint32_t last_ovf_reported = 0;

/* 写一个包进当前拼装块(头8B+payload); 放不下返回0(调用方先flush再试/丢弃) */
static uint8_t in_put_packet(uint8_t type, uint8_t ch, uint16_t count,
                             const uint8_t *payload, uint16_t plen)
{
    if (in_len + 8u + plen > CAN_USB_BLOCK_SIZE) {
        return 0;
    }
    uint8_t *p = &in_blocks[in_cur][in_len];
    p[0] = type;
    p[1] = ch;
    p[2] = (uint8_t)dev_seq;
    p[3] = (uint8_t)(dev_seq >> 8);
    p[4] = (uint8_t)count;
    p[5] = (uint8_t)(count >> 8);
    p[6] = 0;
    p[7] = 0;
    if (plen) {
        memcpy(&p[8], payload, plen);
    }
    dev_seq++;
    in_len += 8u + plen;
    return 1;
}

/* 尝试发送: 在途时不动; ZLP优先; 空块不发 */
static void in_try_flush(void)
{
    if (in_flight) {
        return;
    }
    if (zlp_pending) {
        zlp_pending = 0;
        in_flight = 1;
        if (can_in_start_write(0, RT_NULL, 0) != 0) {
            in_flight = 0;
        }
        return;
    }
    if (in_len == 0) {
        return;
    }
    uint32_t send_len = in_len;
    uint8_t *blk = in_blocks[in_cur];
    in_cur = (in_cur + 1) % CAN_USB_IN_BLOCKS;
    in_len = 0;
    in_flight = 1;
    if (can_in_start_write(0, blk, send_len) != 0) {
        in_flight = 0;
    } else if (send_len == CAN_USB_BLOCK_SIZE) {
        zlp_pending = 1;   /* 512整块: 完成后先补ZLP */
    }
}

/* bulk IN完成: 链式续发(bsp_usb层注册为IN端点回调) */
void can_bridge_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    (void)nbytes;
    in_flight = 0;
    rt_sem_release(bsp_can_activity_sem());
}

/* "放入包, 放不下就冲一块再试, 在途则整包丢弃" 的统一封装 */
static void in_put_or_drop(uint8_t type, uint8_t ch, uint16_t count,
                           const uint8_t *payload, uint16_t plen)
{
    if (in_put_packet(type, ch, count, payload, plen)) {
        return;
    }
    in_try_flush();
    if (in_flight) {
        return;   /* 丢弃(seq可检测) */
    }
    (void)in_put_packet(type, ch, count, payload, plen);
}

/* ---- OUT(usb->can)解析 ---- */

static void handle_ctrl(uint8_t ch, const uint8_t *payload, uint16_t plen)
{
    uint8_t cmd = payload[0];
    uint8_t status = 0;
    uint8_t rsp[24];
    uint16_t rsp_len = 2;

    rsp[0] = cmd;
    rsp[1] = 0;

    switch (cmd) {
    case CANC_GET_INFO:
        rsp[2] = CAN_PROTO_VER;
        rsp[3] = CAN_CH_NUM;
        rsp_len = 4;
        break;
    case CANC_SET_BITRATE: {
        if (plen < 5 || ch >= CAN_CH_NUM) {
            status = 0xFF;
            break;
        }
        uint32_t kbps = payload[1] | (payload[2] << 8) |
                        ((uint32_t)payload[3] << 16) | ((uint32_t)payload[4] << 24);
        status = (bsp_can_set_bitrate(ch, kbps) == 0) ? 0 : 0xFE;
        break;
    }
    case CANC_SET_MODE: {
        if (plen < 2 || ch >= CAN_CH_NUM) {
            status = 0xFF;
            break;
        }
        status = (bsp_can_set_mode(ch, payload[1]) == 0) ? 0 : 0xFE;
        break;
    }
    case CANC_GET_STATUS: {
        if (ch >= CAN_CH_NUM) {
            status = 0xFF;
            break;
        }
        /* 逐字节装配: 曾用uint32_t*直写rsp[2]起(栈上偏移2, 天然2-mod-4未对齐),
         * 栈布局漂移后触发对齐陷阱HardFault杀死桥线程(昨日布局碰巧无害故未暴露) */
        uint32_t vals[5];
        vals[0] = (ch == CAN_CH1) ? g_dbg.can1_tx : g_dbg.can2_tx;
        vals[1] = (ch == CAN_CH1) ? g_dbg.can1_rx : g_dbg.can2_rx;
        vals[2] = (ch == CAN_CH1) ? g_dbg.can1_err : g_dbg.can2_err;
        vals[3] = bsp_can_get_bitrate(ch);
        vals[4] = bsp_can_get_mode(ch);
        for (uint32_t k = 0; k < 5; k++) {
            rsp[2 + 4 * k] = (uint8_t)vals[k];
            rsp[3 + 4 * k] = (uint8_t)(vals[k] >> 8);
            rsp[4 + 4 * k] = (uint8_t)(vals[k] >> 16);
            rsp[5 + 4 * k] = (uint8_t)(vals[k] >> 24);
        }
        rsp_len = 2 + 20;
        break;
    }
    default:
        status = 0xFD;
        break;
    }

    rsp[1] = status;
    in_put_or_drop(CANP_CTRL_RSP, ch, rsp_len, rsp, rsp_len);   /* count=应答payload长度 */
}

static void handle_out_block(const uint8_t *blk, uint16_t len)
{
    uint16_t off = 0;
    while (off + 8u <= len) {
        const uint8_t *h = &blk[off];
        uint8_t type = h[0];
        uint8_t ch = h[1];
        uint16_t count = (uint16_t)(h[4] | (h[5] << 8));
        uint16_t plen;

        if (type == CANP_TX) {
            plen = (uint16_t)(count * 16u);
        } else if (type == CANP_CTRL) {
            plen = count;   /* count复用为payload长度(1..255, CTRL包永远单包小命令) */
        } else if (type == CANP_SOFTRESET) {
            plen = 0;
        } else {
            break;   /* 未知类型: 丢弃本块剩余, 防错位蔓延 */
        }
        if ((uint32_t)off + 8u + plen > len) {
            break;   /* 尾部截断: 丢弃 */
        }
        const uint8_t *payload = &blk[off + 8];

        if (type == CANP_TX && ch < CAN_CH_NUM) {
            for (uint16_t i = 0; i < count; i++) {
                const uint8_t *rec = &payload[i * 16];
                can_frame_t f;
                f.id = rec[0] | (rec[1] << 8) | ((uint32_t)rec[2] << 16) | ((uint32_t)rec[3] << 24);
                f.flags = rec[4];
                f.dlc = rec[5] > 8 ? 8 : rec[5];
                f.handle = (uint16_t)(rec[6] | (rec[7] << 8));
                memcpy(f.data, &rec[8], 8);
                f.ts_us = 0;
                if (!bsp_can_send(ch, &f)) {
                    can_ack_t a = {f.handle, ch, CAN_ACK_DROPPED};
                    bsp_can_ack_push(&a);   /* 环满: 补DROPPED回执 */
                }
            }
        } else if (type == CANP_CTRL) {
            handle_ctrl(ch, payload, plen);
        } else if (type == CANP_SOFTRESET) {
            dev_seq = 0;
        }
        off += 8u + plen;
    }
}

/* ---- IN(can->usb)拼装 ---- */

#define RX_MAX_RECORDS 25    /* (512-8)/20: 单块满载上限, 背压后实际条数由剩余空间决定 */

static void assemble_rx(uint8_t ch)
{
    static __attribute__((aligned(4))) uint8_t stage[RX_MAX_RECORDS * 20];
    uint16_t n = 0;
    uint16_t maxrec = 0;
    can_frame_t f;

    /* 空间预判背压: 当前拼装块剩余空间能装几条才pop几条,
     * 装不下的帧留在接收环等下一轮 -- in_put_or_drop的整包丢弃
     * 从此理论上不可达, 真背压点移到接收环满(有计数+事件上报) */
    if (in_len + 8u + 20u <= CAN_USB_BLOCK_SIZE) {
        maxrec = (uint16_t)((CAN_USB_BLOCK_SIZE - in_len - 8u) / 20u);
        if (maxrec > RX_MAX_RECORDS) {
            maxrec = RX_MAX_RECORDS;
        }
    }
    while (n < maxrec && bsp_can_rx_pop(ch, &f)) {
        uint8_t *rec = &stage[n * 20];
        rec[0] = (uint8_t)f.id;
        rec[1] = (uint8_t)(f.id >> 8);
        rec[2] = (uint8_t)(f.id >> 16);
        rec[3] = (uint8_t)(f.id >> 24);
        rec[4] = f.flags;
        rec[5] = f.dlc;
        rec[6] = 0;
        rec[7] = 0;
        uint32_t ts = f.ts_us;
        rec[8] = (uint8_t)ts;
        rec[9] = (uint8_t)(ts >> 8);
        rec[10] = (uint8_t)(ts >> 16);
        rec[11] = (uint8_t)(ts >> 24);
        memcpy(&rec[12], f.data, 8);
        n++;
    }
    if (n) {
        in_put_or_drop(CANP_RX, ch, n, stage, (uint16_t)(n * 20));
    }
}

#define ACK_MAX_RECORDS 16  /* ack环深度即上限(stage瘦身, RAM紧张) */

static void assemble_acks(void)
{
    static __attribute__((aligned(4))) uint8_t stage[ACK_MAX_RECORDS * 4];
    uint16_t n = 0;
    uint16_t maxrec = 0;
    can_ack_t a;

    /* 同assemble_rx: 空间预判, 装不下的留在ack环(队列深度即缓冲) */
    if (in_len + 8u + 4u <= CAN_USB_BLOCK_SIZE) {
        maxrec = (uint16_t)((CAN_USB_BLOCK_SIZE - in_len - 8u) / 4u);
    }
    while (n < maxrec && bsp_can_ack_pop(&a)) {
        uint8_t *rec = &stage[n * 4];
        rec[0] = (uint8_t)a.handle;
        rec[1] = (uint8_t)(a.handle >> 8);
        rec[2] = a.status;
        rec[3] = a.ch;
        n++;
    }
    if (n) {
        in_put_or_drop(CANP_TXACK, 0, n, stage, (uint16_t)(n * 4));
    }
}

static void assemble_events(void)
{
    if (g_dbg.can_rx_drop != last_ovf_reported) {
        uint8_t rec[8];
        uint32_t v = g_dbg.can_rx_drop;
        rec[0] = CANE_RX_OVF;
        rec[1] = 0;
        rec[2] = (uint8_t)v;
        rec[3] = (uint8_t)(v >> 8);
        rec[4] = (uint8_t)(v >> 16);
        rec[5] = (uint8_t)(v >> 24);
        rec[6] = 0;
        rec[7] = 0;
        last_ovf_reported = g_dbg.can_rx_drop;
        in_put_or_drop(CANP_EVENT, 0, 1, rec, 8);
    }
}

/* ---- 主循环 ---- */

static void can_bridge_thread_entry(void *param)
{
    (void)param;
    uint8_t *blk;
    uint16_t blen;

    rt_thread_mdelay(100);
    while (1) {
        /* 1. OUT块解析(usb->can) */
        while (can_out_pop(&blk, &blen)) {
            handle_out_block(blk, blen);
            can_out_recycle(blk);
            can_out_try_resume();
        }

        /* 2. IN拼装: 回执 -> RX帧 -> 事件 */
        assemble_acks();
        assemble_rx(CAN_CH1);
        assemble_rx(CAN_CH2);
        assemble_events();

        /* 3. 冲一块(有数据且无在途即发) */
        in_try_flush();

        /* 4. 等活动(OUT块/rx帧/回执/IN完成都唤醒; LED归monitor统一渲染) */
        rt_sem_take(bsp_can_activity_sem(), 2);   /* 2ms兜底轮询 */
    }
}

/* 向下注册(早于bsp_usb层INIT_ENV的usbd_initialize) + OUT池初始化 */
int can_bridge_env_init(void)
{
    canusb_handle.in_ep.ep_addr = CAN_IF_IN_EP;
    canusb_handle.out_ep.ep_addr = CAN_IF_OUT_EP;
    canusb_handle.out_ep.ep_cb = can_out_bulk_out;
    canusb_handle.in_ep.ep_cb = can_bridge_bulk_in;   /* IN链式续发 */
    canusb_handle.cfg_cb = can_usb_cfg_cb;
    bsp_pool_init(&canusb_handle.out_pool, canusb_out_pool_storage, CAN_USB_BLOCK_SIZE, CAN_USB_OUT_BLOCKS);
    return 0;
}
INIT_COMPONENT_EXPORT(can_bridge_env_init);

int can_bridge_thread_init(void)
{
    rt_thread_init(&can_bridge_thread,
                   "can_bridge",
                   can_bridge_thread_entry, RT_NULL,
                   can_bridge_stack, sizeof(can_bridge_stack),
                   CAN_BRIDGE_THREAD_PRIORITY, 10);
    rt_thread_startup(&can_bridge_thread);
    return 0;
}
INIT_APP_EXPORT(can_bridge_thread_init);
