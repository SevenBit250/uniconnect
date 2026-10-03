/* uniconnect_upgrade.c -- 固件升级库实现(第3期: 全链真实现)
 *
 * 两阶段(与 tools/fw_update.py 等价, 对拍基准):
 *   阶段1(APP态): open_dap -> Vendor0 -> 设备写BOINIT+复位, APP消失
 *   阶段2(boot态): 轮询scan等0x0219 -> open_hid -> GET_INFO握手
 *     -> BEGIN(len+crc) -> PROGRAM(56B包, 不跨256B页界) -> FLUSH
 *     -> VERIFY(设备全区CRC32对账) -> END(写FLAG_APP_GOOD) -> REBOOT(跳APP)
 * 中断恢复: 任何一步失败/被cb中止, 设备留在boot态(P0: 无GOOD记录永不跳转),
 *   重新调用 fwu_upgrade_file 即完整重做(检测已在boot则跳过阶段1)。
 * 协议常量直引固件 boot/User/boot.h(单一事实源)。
 * 应答包: [cmd|0x80][seq][status][data...] */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "uniconnect_upgrade.h"
#include "boot.h"          /* 固件 boot/User/boot.h: HID升级协议命令字(host-safe) */
#include "uni_transport.h"

#ifdef _WIN32
#include <windows.h>
static uint32_t now_ms(void) { return (uint32_t)GetTickCount64(); }
static void Sleep_(unsigned ms) { Sleep(ms); }
#else
#include <time.h>
static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}
static void Sleep_(unsigned ms)
{
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#endif

struct fwu_ctx {
    uni_dev *hid;                    /* boot HID会话(fwu_wait_bootloader建立) */
    uint8_t seq;
    uint8_t proto;                   /* GET_INFO缓存 */
    uint16_t flash_kb;
    uint32_t app_base, max_img;
};

uint32_t fwu_abi_version(void)
{
    return UNI_ABI_VERSION;
}

const char *fwu_strerror(int err)
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
    case UNI_ERR_ABORTED:  return "aborted by progress callback";
    default:               return "unknown error";
    }
}

/* 标准CRC32(poly 0xEDB88320, init/xor 0xFFFFFFFF) -- 与boot端VERIFY同源 */
static uint32_t crc32_calc(const uint8_t *p, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

static int bps_map(uint8_t st)
{
    switch (st) {
    case BPS_OK:        return UNI_OK;
    case BPS_CRC:       return UNI_ERR_CRC;
    case BPS_STATE:     return UNI_ERR_STATE;
    case BPS_OVERFLOW:  return UNI_ERR_PARAM;
    default:            return UNI_ERR_IO;
    }
}

static void put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t get_u32le(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* HID命令收发: 应答匹配(cmd|0x80, seq)后返回BPS_*映射码, rsp带回后64B应答 */
static int xfer(fwu_ctx *c, uint8_t cmd, const uint8_t *payload, uint8_t plen,
                uint32_t addr, uint8_t rsp[64])
{
    if (!c || !c->hid) return UNI_ERR_STATE;
    if (plen > BP_PAYLOAD_MAX) return UNI_ERR_PARAM;
    uint8_t pkt[64] = {0};
    pkt[BP_OFF_CMD] = cmd;
    pkt[BP_OFF_SEQ] = c->seq;
    pkt[BP_OFF_LEN] = plen;
    put_u32le(pkt + BP_OFF_ADDR, addr);
    if (plen) memcpy(pkt + BP_OFF_PAYLOAD, payload, plen);
    int r = uni_transport_hid_write(c->hid, pkt, 100);
    if (r != UNI_OK) return r;
    uint8_t buf[64];
    for (int i = 0; i < 15; i++) {          /* 15x200ms=3s 应答窗 */
        r = uni_transport_hid_read(c->hid, buf, 200);
        if (r == UNI_ERR_TIMEOUT) continue;
        if (r != UNI_OK) return r;
        if ((buf[0] & 0x7Fu) == cmd && buf[1] == c->seq) {
            if (rsp) memcpy(rsp, buf, 64);
            c->seq = (uint8_t)(c->seq + 1u);
            return bps_map(buf[2]);
        }
        /* 不匹配的应答(陈旧包): 继续等 */
    }
    return UNI_ERR_TIMEOUT;
}

int fwu_open(fwu_ctx **ctx)
{
    if (!ctx) return UNI_ERR_PARAM;
    *ctx = calloc(1, sizeof(fwu_ctx));
    return *ctx ? UNI_OK : UNI_ERR_NOMEM;
}

void fwu_close(fwu_ctx *ctx)
{
    if (!ctx) return;
    if (ctx->hid) uni_transport_close(ctx->hid);
    free(ctx);
}

int fwu_device_state(void)
{
    switch (uni_transport_scan()) {
    case UNI_DEV_APP:  return 0;
    case UNI_DEV_BOOT: return 1;
    default:           return UNI_ERR_NOT_FOUND;
    }
}

int fwu_trigger_bootloader(fwu_ctx *ctx, unsigned timeout_ms)
{
    (void)ctx;
    (void)timeout_ms;                       /* 触发本身<2s, 参数留给未来轮询窗口 */
    uni_dev *d = NULL;
    int r = uni_transport_open_dap(&d);
    if (r != UNI_OK) return r;
    r = uni_transport_dap_vendor0(d);       /* 设备~100ms后复位进boot */
    uni_transport_close(d);
    return r;
}

int fwu_wait_bootloader(fwu_ctx *ctx, unsigned timeout_ms)
{
    if (!ctx) return UNI_ERR_PARAM;
    if (ctx->hid) return UNI_OK;            /* 已在会话中(幂等) */
    uint32_t end = now_ms() + timeout_ms;
    for (;;) {
        if (uni_transport_scan() != UNI_DEV_BOOT) {
            if (now_ms() >= end) return UNI_ERR_TIMEOUT;
            /* 空转10ms */ Sleep_(10);
            continue;
        }
        int r = uni_transport_open_hid(&ctx->hid);
        if (r == UNI_ERR_NOT_FOUND) {       /* 枚举竞争: 再试 */
            if (now_ms() >= end) return UNI_ERR_TIMEOUT;
            Sleep_(10);
            continue;
        }
        if (r != UNI_OK) return r;
        /* GET_INFO握手+缓存分区事实 */
        uint8_t rsp[64];
        r = xfer(ctx, BPC_GET_INFO, NULL, 0, 0, rsp);
        if (r != UNI_OK) {
            uni_transport_close(ctx->hid);
            ctx->hid = NULL;
            return r;
        }
        ctx->proto    = rsp[3];
        ctx->flash_kb = (uint16_t)(rsp[4] | (rsp[5] << 8));
        ctx->app_base = get_u32le(rsp + 7);
        ctx->max_img  = get_u32le(rsp + 11);
        if (ctx->proto != BOOT_PROTO_VER) {
            uni_transport_close(ctx->hid);
            ctx->hid = NULL;
            return UNI_ERR_PROTO;
        }
        return UNI_OK;
    }
}

int fwu_flash(fwu_ctx *ctx, const uint8_t *image, uint32_t len,
              fwu_progress_cb cb, void *user)
{
    if (!ctx || !image || !len) return UNI_ERR_PARAM;
    if (len > (uint32_t)APP_MAX_SIZE || (ctx->hid && len > ctx->max_img))
        return UNI_ERR_PARAM;
    if (!ctx->hid) return UNI_ERR_STATE;    /* 须先 fwu_wait_bootloader */

    uint32_t crc = crc32_calc(image, len);
    uint8_t pl[8], rsp[64];
    put_u32le(pl, len);
    put_u32le(pl + 4, crc);
    int r = xfer(ctx, BPC_BEGIN, pl, 8, APP_BASE, rsp);
    if (r != UNI_OK) return r;

    uint32_t addr = APP_BASE, off = 0;
    while (off < len) {
        /* 单包不跨256B页界(固件页缓冲约束, 56不能整除256) */
        uint32_t page_left = 256u - (addr & 0xFFu);
        uint32_t n = BP_PAYLOAD_MAX;
        if (page_left < n) n = page_left;
        if (len - off < n) n = len - off;
        r = xfer(ctx, BPC_PROGRAM, image + off, (uint8_t)n, addr, rsp);
        if (r != UNI_OK) return r;
        addr += n;
        off += n;
        if (cb) {
            int abort = cb(off, len, user);
            if (abort) return UNI_ERR_ABORTED;   /* 干净中止: 设备留boot可重试 */
        }
    }
    r = xfer(ctx, BPC_FLUSH, NULL, 0, 0, rsp);
    if (r != UNI_OK) return r;

    r = xfer(ctx, BPC_VERIFY, NULL, 0, 0, rsp);  /* 设备全APP区CRC32对账 */
    if (r != UNI_OK) return r;

    r = xfer(ctx, BPC_END, NULL, 0, 0, rsp);     /* 写FLAG_APP_GOOD记录 */
    if (r != UNI_OK) return r;

    r = xfer(ctx, BPC_REBOOT, NULL, 0, 0, rsp);  /* 跳APP */
    if (r != UNI_OK) return r;

    uni_transport_close(ctx->hid);               /* 会话完结 */
    ctx->hid = NULL;
    return UNI_OK;
}

int fwu_reboot(fwu_ctx *ctx)
{
    if (!ctx) return UNI_ERR_PARAM;
    if (!ctx->hid) return UNI_ERR_STATE;
    uint8_t rsp[64];
    int r = xfer(ctx, BPC_REBOOT, NULL, 0, 0, rsp);
    if (r != UNI_OK) return r;
    uni_transport_close(ctx->hid);
    ctx->hid = NULL;
    return UNI_OK;
}

int fwu_upgrade_file(const char *path, fwu_progress_cb cb, void *user,
                     char *err_detail, size_t err_detail_sz)
{
    if (!path) return UNI_ERR_PARAM;
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err_detail) snprintf(err_detail, err_detail_sz, "cannot open %s", path);
        return UNI_ERR_PARAM;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || (uint32_t)sz > (uint32_t)APP_MAX_SIZE) {
        if (err_detail) snprintf(err_detail, err_detail_sz,
                                 "bad image size %ld (max %u)", sz, (unsigned)APP_MAX_SIZE);
        fclose(f);
        return UNI_ERR_PARAM;
    }
    uint8_t *img = malloc((size_t)sz);
    if (!img) { fclose(f); return UNI_ERR_NOMEM; }
    if (fread(img, 1, (size_t)sz, f) != (size_t)sz) {
        if (err_detail) snprintf(err_detail, err_detail_sz, "read error");
        free(img);
        fclose(f);
        return UNI_ERR_IO;
    }
    fclose(f);

    fwu_ctx *c = NULL;
    int r = fwu_open(&c);
    if (r == UNI_OK) {
        int st = fwu_device_state();
        if (st == 0) {                          /* APP态: 阶段1触发 */
            r = fwu_trigger_bootloader(c, 3000);
        } else if (st < 0 && st != UNI_ERR_NOT_FOUND) {
            r = st;                             /* 扫描IO错误 */
        } /* st==1已在boot: 中断恢复路径, 直接阶段2 */
        if (r == UNI_OK) {
            r = fwu_wait_bootloader(c, 15000);
            if (r == UNI_OK)
                r = fwu_flash(c, img, (uint32_t)sz, cb, user);
        }
        fwu_close(c);
    }
    free(img);
    if (r != UNI_OK && err_detail)
        snprintf(err_detail, err_detail_sz, "%s (%d)", fwu_strerror(r), r);
    return r;
}
