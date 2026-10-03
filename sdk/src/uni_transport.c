/* uni_transport.c -- 传输层实现(libusb后端)
 *
 * 第2期: scan/open_can/open_dap/bulk读写/关闭 已真实现(静态链 vendored libusb v1.0.30,
 * Windows WinUSB后端); HID通路(boot态升级)与dap_vendor0 第3期接hidapi时点亮。
 * 上下文生命周期: libusb context 进程级单例, 只增不减(DLL卸载场景由OS回收)。
 * 线程性: libusb自身线程安全; uni_dev句柄遵守ABI"一通道一线程"由上层约束。 */
#include <stdlib.h>
#include "uniconnect_common.h"
#include "uni_transport.h"
#include "usb_ids.h"
#include "libusb.h"

struct uni_dev {
    libusb_device_handle *usb;
    uint8_t intf;         /* 已claim的接口号 */
};

static libusb_context *g_ctx;

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
    if (!dev || !buf || !len) return UNI_ERR_PARAM;
    int tx = 0;
    int r = libusb_bulk_transfer(dev->usb, ep, (unsigned char *)buf, (int)len,
                                 &tx, timeout_ms);
    if (r != LIBUSB_SUCCESS) return map_err(r);
    return tx == (int)len ? UNI_OK : UNI_ERR_IO;
}

int uni_transport_bulk_read(uni_dev *dev, uint8_t ep, uint8_t *buf, size_t len,
                            unsigned timeout_ms, size_t *out_len)
{
    if (!dev || !buf || !len || !out_len) return UNI_ERR_PARAM;
    int rx = 0;
    int r = libusb_bulk_transfer(dev->usb, ep, buf, (int)len, &rx, timeout_ms);
    if (r != LIBUSB_SUCCESS) return map_err(r);
    *out_len = (size_t)rx;
    return UNI_OK;
}

int uni_transport_open_hid(uni_dev **dev)
{
    (void)dev;
    return UNI_ERR_NOT_SUP;   /* 第3期: hidapi */
}

int uni_transport_hid_write(uni_dev *dev, const uint8_t report[64], unsigned timeout_ms)
{
    (void)dev; (void)report; (void)timeout_ms;
    return UNI_ERR_NOT_SUP;
}

int uni_transport_hid_read(uni_dev *dev, uint8_t report[64], unsigned timeout_ms)
{
    (void)dev; (void)report; (void)timeout_ms;
    return UNI_ERR_NOT_SUP;
}

int uni_transport_dap_vendor0(uni_dev *dev)
{
    (void)dev;
    return UNI_ERR_NOT_SUP;   /* 第3期: DAP bulk写0x80触发 */
}

void uni_transport_close(uni_dev *dev)
{
    if (!dev) return;
    libusb_release_interface(dev->usb, dev->intf);
    libusb_close(dev->usb);
    free(dev);
}
