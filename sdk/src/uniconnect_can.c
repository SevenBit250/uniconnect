/* uniconnect_can.c -- CAN 通讯库实现(第2期: libusb传输 + 协议真实现)
 *
 * 数据面模型: 单线程pump -- 每次bulk IN读回0..N个协议包(固件512B拼装块可含多包,
 * 且包可能跨USB传输边界), 暂存区缝合后逐包分发:
 *   CANP_RX     -> 双通道各自帧环(1024深, can_send等待期持续收割防溢出)
 *   CANP_TXACK  -> 单一在途send的handle对账(status=CAN_ACK_*)
 *   CANP_CTRL_RSP-> 单一在途ctrl调用的应答暂存
 *   CANP_EVENT  -> 计数(CANE_RX_OVF=设备侧累计丢弃, 测试对账用)
 * 协议常量/包格式与固件 common/can_proto.h 单一事实源。
 * 时间: now_ms 用 GetTickCount64(Windows)/clock_gettime(Linux)。 */
#include <stdlib.h>
#include <string.h>
#include "uniconnect_can.h"
#include "uni_transport.h"
#include "can_proto.h"
#include "usb_ids.h"

#ifdef _WIN32
#include <windows.h>
static uint32_t now_ms(void) { return (uint32_t)GetTickCount64(); }
#else
#include <time.h>
static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}
#endif

#define CAN_RX_RING_DEPTH 1024u   /* 每通道帧环: send等待期pump持续收割 */
#define CAN_USB_BLOCK     512u    /* = 固件CAN_USB_BLOCK_SIZE */
#define CTRL_TIMEOUT_MS   300u    /* CTRL应答等待(固件毫秒级, 300ms裕量) */
#define CTRL_RSP_CAP      64u     /* CTRL_RSP payload上限(GET_STATUS=20B) */

struct can_rx_ent {
    uint32_t id;
    uint8_t  flags;
    uint8_t  dlc;
    uint8_t  data[8];
};

struct can_ctx {
    uni_dev *dev;
    struct can_rx_ent ring[2][CAN_RX_RING_DEPTH];
    uint16_t r_head[2], r_tail[2];   /* head=写, tail=读 */
    uint32_t r_drop[2];              /* 主机侧环满丢弃累计 */
    uint32_t dev_ovf;                /* CANE_RX_OVF: 设备侧累计丢弃(最后一次值) */
    uint8_t  stage[CAN_USB_BLOCK]; uint16_t stage_len;   /* 跨读残余缝合区 */
    struct {
        uint8_t buf[CTRL_RSP_CAP]; uint16_t len;
        int valid;
    } ctrl_rsp;
    struct {
        int waiting, done;
        uint16_t handle;
        uint8_t status;
    } ack;
    uint16_t next_handle;
};

/* ---- 小端装配 ---- */
static void put_hdr(uint8_t *h, uint8_t type, uint8_t ch, uint16_t count)
{
    h[0] = type; h[1] = ch;
    h[2] = 0; h[3] = 0;             /* OUT seq固件忽略 */
    h[4] = (uint8_t)count; h[5] = (uint8_t)(count >> 8);
    h[6] = 0; h[7] = 0;
}
static void put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint16_t get_u16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t get_u32le(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* 单个完整包分发(入参len=8+payload, 已校验) */
static void ingest_packet(can_ctx *c, const uint8_t *p, uint16_t len)
{
    uint8_t type = p[0], ch = p[1];
    uint16_t count = get_u16le(p + 4);
    const uint8_t *pl = p + 8;

    switch (type) {
    case CANP_RX:
        if (ch > 1) break;
        for (uint16_t i = 0; i < count && (uint32_t)(i + 1) * 20u <= (uint32_t)len - 8u; i++) {
            const uint8_t *r = pl + i * 20u;
            if ((uint16_t)((c->r_head[ch] + 1u) % CAN_RX_RING_DEPTH) == c->r_tail[ch]) {
                c->r_drop[ch] += count - i;   /* 环满: 后续整段记丢弃 */
                return;
            }
            struct can_rx_ent *e = &c->ring[ch][c->r_head[ch]];
            e->id = get_u32le(r);
            e->flags = r[4];
            e->dlc = r[5] > 8 ? 8 : r[5];
            memcpy(e->data, r + 12, 8);
            c->r_head[ch] = (uint16_t)((c->r_head[ch] + 1u) % CAN_RX_RING_DEPTH);
        }
        break;
    case CANP_TXACK:
        for (uint16_t i = 0; i < count && (uint32_t)(i + 1) * 4u <= (uint32_t)len - 8u; i++) {
            const uint8_t *r = pl + i * 4u;
            if (c->ack.waiting && get_u16le(r) == c->ack.handle) {
                c->ack.status = r[2];
                c->ack.done = 1;
            }
        }
        break;
    case CANP_EVENT:
        for (uint16_t i = 0; i < count && (uint32_t)(i + 1) * 8u <= (uint32_t)len - 8u; i++) {
            if (pl[i * 8u] == CANE_RX_OVF)
                c->dev_ovf = get_u32le(pl + i * 8u + 2);   /* 累计值: 存最后一次 */
        }
        break;
    case CANP_CTRL_RSP:
        if (count <= CTRL_RSP_CAP) {
            memcpy(c->ctrl_rsp.buf, pl, count);
            c->ctrl_rsp.len = count;
            c->ctrl_rsp.valid = 1;
        }
        break;
    default:
        break;
    }
}

static int plen_of(uint8_t type, uint16_t count)
{
    switch (type) {
    case CANP_RX:       return count * 20;
    case CANP_TXACK:    return count * 4;
    case CANP_EVENT:    return count * 8;
    case CANP_CTRL_RSP: return count;       /* count=应答payload总长(含cmd) */
    default:            return -1;          /* 未知类型: 丢弃防错位(与固件同策) */
    }
}

/* bulk IN读一次并分发; 返回UNI_OK或错误(TIMEOUT=正常排空) */
static int pump(can_ctx *c, unsigned timeout_ms)
{
    uint8_t buf[CAN_USB_BLOCK];
    uint8_t parse[CAN_USB_BLOCK * 2];
    size_t got = 0;
    int r = uni_transport_bulk_read(c->dev, UNI_CAN_IN_EP, buf, sizeof buf,
                                    timeout_ms, &got);
    if (r != UNI_OK) return r;

    if ((size_t)c->stage_len + got > sizeof parse) {
        c->stage_len = 0;
        return UNI_ERR_PROTO;               /* 暂存超限=流错位 */
    }
    memcpy(parse, c->stage, c->stage_len);
    memcpy(parse + c->stage_len, buf, got);
    size_t off = 0, total = (size_t)c->stage_len + got;
    while (off + 8 <= total) {
        int plen = plen_of(parse[off], get_u16le(parse + off + 4));
        if (plen < 0) { off = total; break; }
        if (off + 8u + (size_t)plen > total) break;   /* 残余: 留暂存待下读缝合 */
        ingest_packet(c, parse + off, (uint16_t)(8u + (size_t)plen));
        off += 8u + (size_t)plen;
    }
    c->stage_len = (uint16_t)(total - off);
    memmove(c->stage, parse + off, c->stage_len);
    return UNI_OK;
}

/* CTRL同步调用: rsp格式[cmd:1][status:1][body...], rsp_cap入参=缓冲容量 */
static int ctrl_call(can_ctx *c, uint8_t ch, uint8_t cmd,
                     const uint8_t *pl, uint8_t pln,
                     uint8_t *rsp, uint8_t *rsp_len)
{
    if (!c->dev) return UNI_ERR_STATE;
    uint8_t pkt[8 + 1 + 4];                 /* 最大payload: SET_BITRATE u32 */
    put_hdr(pkt, CANP_CTRL, ch, (uint16_t)(1u + pln));
    pkt[8] = cmd;
    if (pln) memcpy(pkt + 9, pl, pln);
    c->ctrl_rsp.valid = 0;
    int r = uni_transport_bulk_write(c->dev, UNI_CAN_OUT_EP, pkt, 8u + 1u + pln, 500);
    if (r != UNI_OK) return r;

    uint32_t end = now_ms() + CTRL_TIMEOUT_MS;
    for (;;) {
        r = pump(c, 20);
        if (r != UNI_OK && r != UNI_ERR_TIMEOUT) return r;
        if (c->ctrl_rsp.valid) break;
        if (now_ms() >= end) return UNI_ERR_TIMEOUT;
    }
    if (c->ctrl_rsp.len < 2 || c->ctrl_rsp.buf[0] != cmd)
        return UNI_ERR_PROTO;
    if (c->ctrl_rsp.buf[1] != 0)
        return UNI_ERR_IO;                  /* 设备侧命令执行失败 */
    if (c->ctrl_rsp.len - 2u > *rsp_len) return UNI_ERR_PROTO;
    memcpy(rsp, c->ctrl_rsp.buf + 2, c->ctrl_rsp.len - 2u);
    *rsp_len = (uint8_t)(c->ctrl_rsp.len - 2u);
    return UNI_OK;
}

uint32_t can_abi_version(void)
{
    return UNI_ABI_VERSION;
}

const char *can_strerror(int err)
{
    switch (err) {
    case UNI_OK:           return "ok";
    case UNI_ERR_PARAM:    return "invalid parameter";
    case UNI_ERR_NOMEM:    return "out of memory";
    case UNI_ERR_NOT_FOUND:return "device not found";
    case UNI_ERR_BUSY:     return "device busy";
    case UNI_ERR_IO:       return "usb io error";
    case UNI_ERR_TIMEOUT:  return "timeout";
    case UNI_ERR_NOT_SUP:  return "not supported (this build/platform)";
    case UNI_ERR_PROTO:    return "device protocol error";
    case UNI_ERR_STATE:    return "call sequence error";
    case UNI_ERR_CRC:      return "crc mismatch";
    default:               return "unknown error";
    }
}

int can_open(can_ctx **ctx)
{
    if (!ctx) return UNI_ERR_PARAM;
    can_ctx *c = calloc(1, sizeof(*c));
    if (!c) return UNI_ERR_NOMEM;
    int r = uni_transport_open_can(&c->dev);
    if (r != UNI_OK) { free(c); return r; }
    /* 开门握手: 协议版本/通道数对账 */
    uint8_t proto = 0, ch_num = 0;
    r = can_get_info(c, &proto, &ch_num);
    if (r != UNI_OK) { can_close(c); return r; }
    if (proto != CAN_PROTO_VER || ch_num != 2) { can_close(c); return UNI_ERR_PROTO; }
    *ctx = c;
    return UNI_OK;
}

void can_close(can_ctx *ctx)
{
    if (!ctx) return;
    if (ctx->dev) {
        uint8_t pkt[8];
        put_hdr(pkt, CANP_SOFTRESET, 0, 0);       /* 链路软复位(清dev_seq), 尽力而为 */
        (void)uni_transport_bulk_write(ctx->dev, UNI_CAN_OUT_EP, pkt, 8, 50);
        uni_transport_close(ctx->dev);
    }
    free(ctx);
}

int can_set_mode(can_ctx *ctx, uint8_t ch, uint8_t mode)
{
    if (!ctx || ch > 1) return UNI_ERR_PARAM;
    if (mode != CAN_MODE_LOOPBACK_TEST && mode != CAN_MODE_NORMAL &&
        mode != CAN_MODE_SLEEP) return UNI_ERR_PARAM;
    uint8_t rsp[4]; uint8_t rl = sizeof rsp;
    return ctrl_call(ctx, ch, CANC_SET_MODE, &mode, 1, rsp, &rl);
}

int can_set_bitrate(can_ctx *ctx, uint8_t ch, uint32_t kbps)
{
    if (!ctx || ch > 1 || kbps < 5 || kbps > 1000) return UNI_ERR_PARAM;
    uint8_t pl[4]; uint8_t rsp[4]; uint8_t rl = sizeof rsp;
    put_u32le(pl, kbps);
    return ctrl_call(ctx, ch, CANC_SET_BITRATE, pl, 4, rsp, &rl);
}

int can_send(can_ctx *ctx, uint8_t ch, const can_frame *f, unsigned timeout_ms)
{
    if (!ctx || !f || ch > 1 || f->dlc > 8) return UNI_ERR_PARAM;
    if (!ctx->dev) return UNI_ERR_STATE;
    uint16_t h = (uint16_t)++ctx->next_handle;
    if (h == 0) h = ++ctx->next_handle;
    uint8_t pkt[8 + 16];
    put_hdr(pkt, CANP_TX, ch, 1);
    put_u32le(pkt + 8, f->id);
    pkt[12] = f->flags;
    pkt[13] = f->dlc;
    pkt[14] = (uint8_t)h; pkt[15] = (uint8_t)(h >> 8);
    memcpy(pkt + 16, f->data, 8);
    ctx->ack.waiting = 1;
    ctx->ack.done = 0;
    ctx->ack.handle = h;
    ctx->ack.status = 0xFF;
    int r = uni_transport_bulk_write(ctx->dev, UNI_CAN_OUT_EP, pkt, sizeof pkt, 500);
    if (r != UNI_OK) { ctx->ack.waiting = 0; return r; }

    if (timeout_ms == 0) {
        r = pump(ctx, 1);   /* libusb timeout=0=无限等待, 非阻塞轮询须给1ms */
        if (r != UNI_OK && r != UNI_ERR_TIMEOUT) { ctx->ack.waiting = 0; return r; }
    } else {
        uint32_t end = now_ms() + timeout_ms;
        for (;;) {
            r = pump(ctx, 10);
            if (r != UNI_OK && r != UNI_ERR_TIMEOUT) { ctx->ack.waiting = 0; return r; }
            if (ctx->ack.done) break;
            if (now_ms() >= end) { ctx->ack.waiting = 0; return UNI_ERR_TIMEOUT; }
        }
    }
    ctx->ack.waiting = 0;
    if (!ctx->ack.done) return UNI_ERR_TIMEOUT;
    return ctx->ack.status == 0 ? UNI_OK : UNI_ERR_IO;   /* CAN_ACK_SENT=0, 其余为发送失败 */
}

int can_recv(can_ctx *ctx, uint8_t ch, can_frame *f, unsigned timeout_ms)
{
    if (!ctx || !f || ch > 1) return UNI_ERR_PARAM;
    if (!ctx->dev) return UNI_ERR_STATE;
    uint32_t end = now_ms() + timeout_ms;
    for (;;) {
        if (ctx->r_head[ch] != ctx->r_tail[ch]) {
            struct can_rx_ent *e = &ctx->ring[ch][ctx->r_tail[ch]];
            f->id = e->id; f->flags = e->flags; f->dlc = e->dlc;
            memcpy(f->data, e->data, 8);
            ctx->r_tail[ch] = (uint16_t)((ctx->r_tail[ch] + 1u) % CAN_RX_RING_DEPTH);
            return UNI_OK;
        }
        int r = pump(ctx, timeout_ms ? 10u : 1u);   /* 0=立即返回: 1ms轮询(libusb的0是无限等待) */
        if (r != UNI_OK && r != UNI_ERR_TIMEOUT) return r;
        if (timeout_ms == 0 || now_ms() >= end) return UNI_ERR_TIMEOUT;
    }
}

int can_flush_rx(can_ctx *ctx, uint8_t ch)
{
    if (!ctx || ch > 1) return UNI_ERR_PARAM;
    ctx->r_tail[ch] = ctx->r_head[ch];
    ctx->stage_len = 0;                     /* 半包残余一并丢弃 */
    return UNI_OK;
}

int can_get_status(can_ctx *ctx, uint8_t ch, can_ch_status *st)
{
    if (!ctx || !st || ch > 1) return UNI_ERR_PARAM;
    uint8_t rsp[20]; uint8_t rl = sizeof rsp;
    int r = ctrl_call(ctx, ch, CANC_GET_STATUS, NULL, 0, rsp, &rl);
    if (r != UNI_OK) return r;
    if (rl < 12) return UNI_ERR_PROTO;      /* tx/rx/err u32x3 (后接kbps/mode) */
    st->tx_cnt = get_u32le(rsp);
    st->rx_cnt = get_u32le(rsp + 4);
    st->err_cnt = get_u32le(rsp + 8);
    return UNI_OK;
}

int can_get_info(can_ctx *ctx, uint8_t *proto_ver, uint8_t *ch_num)
{
    if (!ctx || !proto_ver || !ch_num) return UNI_ERR_PARAM;
    uint8_t rsp[4]; uint8_t rl = sizeof rsp;
    int r = ctrl_call(ctx, 0, CANC_GET_INFO, NULL, 0, rsp, &rl);
    if (r != UNI_OK) return r;
    if (rl < 2) return UNI_ERR_PROTO;
    *proto_ver = rsp[0];
    *ch_num = rsp[1];
    return UNI_OK;
}
