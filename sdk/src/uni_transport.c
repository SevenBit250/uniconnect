/* uni_transport.c -- 传输层第1期桩实现
 *
 * 第2期接入 libusb(WinUSB bulk) + hidapi(boot HID) 后逐函数替换;
 * 桩返回 UNI_ERR_NOT_SUP 保证上层API可先行联调/差分框架可编译。
 * 平台矩阵: Windows=libusb-1.0 + hidapi(win后端); Linux=系统libusb+hidapi;
 * macOS=骨架。 */
#include <stdlib.h>
#include "uniconnect_common.h"
#include "uni_transport.h"

uni_dev_state uni_transport_scan(void)
{
    return UNI_DEV_NONE;   /* 桩: 第2期实现VID/PID扫描 */
}

int uni_transport_open_can(uni_dev **dev)
{
    (void)dev;
    return UNI_ERR_NOT_SUP;
}

int uni_transport_open_dap(uni_dev **dev)
{
    (void)dev;
    return UNI_ERR_NOT_SUP;
}

int uni_transport_bulk_write(uni_dev *dev, uint8_t ep, const uint8_t *buf, size_t len, unsigned timeout_ms)
{
    (void)dev; (void)ep; (void)buf; (void)len; (void)timeout_ms;
    return UNI_ERR_NOT_SUP;
}

int uni_transport_bulk_read(uni_dev *dev, uint8_t ep, uint8_t *buf, size_t len, unsigned timeout_ms)
{
    (void)dev; (void)ep; (void)buf; (void)len; (void)timeout_ms;
    return UNI_ERR_NOT_SUP;
}

int uni_transport_open_hid(uni_dev **dev)
{
    (void)dev;
    return UNI_ERR_NOT_SUP;
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
    return UNI_ERR_NOT_SUP;
}

void uni_transport_close(uni_dev *dev)
{
    (void)dev;
}
