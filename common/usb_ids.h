/* usb_ids.h -- USB 标识(VID/PID/接口号/端点)单一事实源 (APP/boot/SDK 共用)
 *
 * host-safe 契约: 只依赖 <stdint.h>, sdk/ 构建即机械检测。
 * 接口号是 usbccgp 子设备号(MI_xx), 端点是 bulk 传输用 libusb 寻址值。
 * 来源: 2026-10-03 自 app/Bsp/bsp_config.h 与 boot/User/usb_glue.c 收敛。 */
#ifndef __USB_IDS_H
#define __USB_IDS_H

#include <stdint.h>

#define UNI_VID            0x0D28   /* ARM ltd 供应商段(与DAP参考设计一致) */

/* PID: APP 与 boot 互斥形态 -- 独立PID使Windows各建全新devnode(无缓存串扰),
 * 升级工具按PID无歧义识别当前形态 */
#define UNI_PID_APP        0x0218
#define UNI_PID_BOOT       0x0219

/* APP 复合设备接口号(usbccgp 子设备, 与 bsp_usb.c 描述符顺序一致;
 * DAP厂商接口必须排在CAN之前: OpenOCD bulk后端取第一个厂商bulk接口) */
#define UNI_IF_CDC_DATA    0
#define UNI_IF_CDC_CTRL    1
#define UNI_IF_DAP         2
#define UNI_IF_CAN         3

/* bulk 端点(SDK/libusb 寻址用; 与 bsp_config.h 同值, 那边经别名引用) */
#define UNI_DAP_OUT_EP     0x02
#define UNI_DAP_IN_EP      0x81
#define UNI_CAN_OUT_EP     0x06
#define UNI_CAN_IN_EP      0x83

/* DAP Vendor0 = fw_upgrade 阶段1触发命令(CMSIS-DAP 厂商命令空间) */
#define UNI_DAP_CMD_FW_TRIGGER  0x80

#endif /* __USB_IDS_H */
