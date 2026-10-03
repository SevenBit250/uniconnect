/* uni_transport.c -- 传输层实现(libusb + hidapi 双后端)
 *
 * 第3期: 全通路真实现——
 *   WinUSB bulk(libusb): scan/open_can/open_dap/bulk读写/dap_vendor0
 *   boot HID(hidapi): open_hid/hid_write/hid_read(64B报告无report-id,
 *     Windows侧hidapi缓冲前置report-id字节0x00, 读回同样带前缀)
 * 上下文生命周期: libusb/hidapi 初始化进程级单例, 只增不减(OS回收)。
 * 线程性: 两库自身线程安全; uni_dev句柄遵守ABI"一通道一线程"由上层约束。 */
#include <stdlib.h>
#include <string.h>
#include "uniconnect_common.h"
#include "uni_transport.h"
#include "usb_ids.h"
#include "libusb.h"
#include "hidapi/hidapi.h"

struct uni_dev {
    libusb_device_handle *usb;   /* bulk通路(非NULL有效) */
    hid_device *hid;             /* HID通路(非NULL有效) */
    uint8_t intf;                /* bulk通路已claim的接口号 */
};

static libusb_context *g_ctx;
static int g_hid_inited;

static int ctx_acquire(void)
{
    if (g_ctx) return UNI_OK;
    return libusb_init(&g_ctx) == LIBUSB_SUCCESS ? UNI_OK : UNI_ERR_IO;
}

static int map_err(int r)
{
    switch (r) {
    case LIBUSB_ERROR_TIMEOUT:   return UNI_ERR_TIMEOUT;
    case LIBUSB_ERROR_NO_DEVICE:
    case LIBUSB_ERROR_NOT_FOUND: return UNI_ERR_NOT_FOUND;
    case LIBUSB_ERROR_BUSY:      return UNI_ERR_BUSY;
    case LIBUSB_ERROR_NO_MEM:    return UNI_ERR_NOMEM;
    case LIBUSB_ERROR_OVERFLOW:  return UNI_ERR_PROTO;
    default:                     return UNI_ERR_IO;
    }
}

uni_dev_state uni_transport_scan(void)
{
    if (ctx_acquire() != UNI_OK) return UNI_DEV_NONE;
    libusb_device **list;
    ssize_t n = libusb_get_device_list(g_ctx, &list);
    uni_dev_state st = UNI_DEV_NONE;
    for (ssize_t i = 0; i < n; i++) {
        struct libusb_device_descriptor d;
        if (libusb_get_device_descriptor(list[i], &d) != LIBUSB_SUCCESS) continue;
        if (d.idVendor != UNI_VID) continue;
        if (d.idProduct == UNI_PID_APP)  { st = UNI_DEV_APP;  break; }
        if (d.idProduct == UNI_PID_BOOT) { st = UNI_DEV_BOOT; break; }
    }
    libusb_free_device_list(list, 1);
    return st;
}

/* 按PID开设备并claim指定接口。复合设备Windows侧=usbccgp子设备(MI_xx),
 * libusb的claim_interface直接映射WinUsb_Initialize, 无需set_configuration。 */
static int open_by_pid_claim(uint16_t pid, uint8_t intf, uni_dev **dev)
{
    if (ctx_acquire() != UNI_OK) return UNI_ERR_IO;
    libusb_device_handle *h = libusb_open_device_with_vid_pid(g_ctx, UNI_VID, pid);
    if (!h) return UNI_ERR_NOT_FOUND;
    libusb_set_auto_detach_kernel_driver(h, 1);   /* Linux自动卸内核驱动; Windows空操作 */
    int r = libusb_claim_interface(h, intf);
    if (r != LIBUSB_SUCCESS) {
        libusb_close(h);
        return map_err(r);
    }
    uni_dev *d = calloc(1, sizeof(*d));
    if (!d) {
        libusb_release_interface(h, intf);
        libusb_close(h);
        return UNI_ERR_NOMEM;
    }
    d->usb = h;
    d->intf = intf;
    *dev = d;
    return UNI_OK;
}

int uni_transport_open_can(uni_dev **dev)
{
    return open_by_pid_claim(UNI_PID_APP, UNI_IF_CAN, dev);
}

int uni_transport_open_dap(uni_dev **dev)
{
    return open_by_pid_claim(UNI_PID_APP, UNI_IF_DAP, dev);
}

int uni_transport_bulk_write(uni_dev *dev, uint8_t ep, const uint8_t *buf,
                             size_t len, unsigned timeout_ms)
{
    if (!dev || !dev->usb || !buf || !len) return UNI_ERR_PARAM;
    int tx = 0;
    int r = libusb_bulk_transfer(dev->usb, ep, (unsigned char *)buf, (int)len,
                                 &tx, timeout_ms);
    if (r != LIBUSB_SUCCESS) return map_err(r);
    return tx == (int)len ? UNI_OK : UNI_ERR_IO;
}

int uni_transport_bulk_read(uni_dev *dev, uint8_t ep, uint8_t *buf, size_t len,
                            unsigned timeout_ms, size_t *out_len)
{
    if (!dev || !dev->usb || !buf || !len || !out_len) return UNI_ERR_PARAM;
    int rx = 0;
    int r = libusb_bulk_transfer(dev->usb, ep, buf, (int)len, &rx, timeout_ms);
    if (r != LIBUSB_SUCCESS) return map_err(r);
    *out_len = (size_t)rx;
    return UNI_OK;
}

/* ---- boot HID通路(hidapi, 64B报告无report-id) ---- */

int uni_transport_open_hid(uni_dev **dev)
{
    if (!dev) return UNI_ERR_PARAM;
    if (!g_hid_inited) {
        if (hid_init() != 0) return UNI_ERR_IO;
        g_hid_inited = 1;
    }
    hid_device *h = hid_open(UNI_VID, UNI_PID_BOOT, NULL);
    if (!h) return UNI_ERR_NOT_FOUND;
    uni_dev *d = calloc(1, sizeof(*d));
    if (!d) { hid_close(h); return UNI_ERR_NOMEM; }
    d->hid = h;
    *dev = d;
    return UNI_OK;
}

int uni_transport_hid_write(uni_dev *dev, const uint8_t report[64], unsigned timeout_ms)
{
    (void)timeout_ms;   /* hidapi写为同步语义, 无超时参数 */
    if (!dev || !dev->hid || !report) return UNI_ERR_PARAM;
    uint8_t buf[1 + 64];                /* Windows要求前置report-id字节(无ID补0x00) */
    buf[0] = 0;
    memcpy(buf + 1, report, 64);
    int n = hid_write(dev->hid, buf, sizeof buf);
    if (n == -1) return UNI_ERR_IO;
    return n == (int)sizeof buf ? UNI_OK : UNI_ERR_IO;
}

int uni_transport_hid_read(uni_dev *dev, uint8_t report[64], unsigned timeout_ms)
{
    if (!dev || !dev->hid || !report) return UNI_ERR_PARAM;
    uint8_t buf[1 + 64];
    for (;;) {
        int n = hid_read_timeout(dev->hid, buf, sizeof buf, timeout_ms ? (int)timeout_ms : 1);
        if (n == 0) return UNI_ERR_TIMEOUT;
        if (n == -1) return UNI_ERR_IO;
        if (n >= 64) {                  /* 前缀可能缺席/多余, 取末64B对齐(与fw_update.py同策) */
            memcpy(report, buf + n - 64, 64);
            return UNI_OK;
        }
        /* 短读(残余噪声): 继续等 */
    }
}

/* DAP厂商命令Vendor0: 写[0x80+63*0]到DAP_OUT, 读DAP_IN校验[0x80][DAP_OK] */
int uni_transport_dap_vendor0(uni_dev *dev)
{
    if (!dev || !dev->usb) return UNI_ERR_PARAM;
    uint8_t cmd[64] = { UNI_DAP_CMD_FW_TRIGGER };
    uint8_t rsp[64] = {0};
    int r = uni_transport_bulk_write(dev, UNI_DAP_OUT_EP, cmd, sizeof cmd, 500);
    if (r != UNI_OK) return r;
    size_t got = 0;
    r = uni_transport_bulk_read(dev, UNI_DAP_IN_EP, rsp, sizeof rsp, 2000, &got);
    if (r != UNI_OK) return r;
    if (got < 2 || rsp[0] != UNI_DAP_CMD_FW_TRIGGER || rsp[1] != 0)
        return UNI_ERR_PROTO;           /* [Vendor0][DAP_OK=0] */
    return UNI_OK;
}

void uni_transport_close(uni_dev *dev)
{
    if (!dev) return;
    if (dev->usb) {
        libusb_release_interface(dev->usb, dev->intf);
        libusb_close(dev->usb);
    }
    if (dev->hid) {
        hid_close(dev->hid);
    }
    free(dev);
}
