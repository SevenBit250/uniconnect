/* uni_transport.h -- SDK 内部传输层(不对外导出)
 *
 * 职责: libusb/hidapi 的全部平台差异收敛于此, uniconnect_upgrade/uniconnect_can 只见
 * 纯净接口。第2期: libusb后端已真实现(WinUSB bulk通路); HID通路(boot升级)第3期接hidapi。
 * 契约: 头文件不引入任何第三方库类型(用前向声明/句柄), 保证上层可测。 */
#ifndef UNI_TRANSPORT_H
#define UNI_TRANSPORT_H

#include <stdint.h>
#include <stddef.h>

/* 打开的USB设备句柄(内部为libusb设备句柄的包装) */
typedef struct uni_dev uni_dev;

/* 设备形态(按PID区分) */
typedef enum {
    UNI_DEV_NONE = 0,     /* 不在线 */
    UNI_DEV_APP,          /* 0x0218 复合设备 */
    UNI_DEV_BOOT,         /* 0x0219 bootloader HID */
} uni_dev_state;

uni_dev_state uni_transport_scan(void);          /* 探测当前在线形态 */

/* WinUSB bulk 通路(APP态, 接口+端点由 usb_ids.h 描述) */
int uni_transport_open_can(uni_dev **dev);
int uni_transport_open_dap(uni_dev **dev);
int uni_transport_bulk_write(uni_dev *dev, uint8_t ep, const uint8_t *buf, size_t len, unsigned timeout_ms);
/* 读到返回UNI_OK且*out_len=实收字节数; 超时返回UNI_ERR_TIMEOUT(不区分读到0字节) */
int uni_transport_bulk_read(uni_dev *dev, uint8_t ep, uint8_t *buf, size_t len,
                            unsigned timeout_ms, size_t *out_len);

/* HID 通路(boot态, 64B报告无report-id) -- 第3期hidapi */
int uni_transport_open_hid(uni_dev **dev);
int uni_transport_hid_write(uni_dev *dev, const uint8_t report[64], unsigned timeout_ms);
int uni_transport_hid_read(uni_dev *dev, uint8_t report[64], unsigned timeout_ms);

/* DAP厂商命令通路(uniconnect_upgrade阶段1) -- 第3期 */
int uni_transport_dap_vendor0(uni_dev *dev);

void uni_transport_close(uni_dev *dev);

#endif /* UNI_TRANSPORT_H */
