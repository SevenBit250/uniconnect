/* uni_defs.h -- UniConnect SDK 公共定义 (跨 fw_upgrade/can_comm 两库)
 *
 * ABI 规约(两库一致, 违反即破坏性变更):
 * - 全部导出函数为 C 链接(extern "C"), 不透明句柄, int 错误码
 * - 缓冲区一律调用者提供(容量入参/实长出参), 库不对调用者 malloc
 * - 字符串 UTF-8; 句柄非线程安全(一通道一线程), 跨线程由调用者加锁
 * - 结构体布局冻结: 只增不改; 新功能加函数, 不动老布局
 *
 * 构建脚本经 CMake 传入 UNI_SDK_BUILD 时导出, 消费端默认导入(Windows);
 * MinGW/GCC 走 .def 显式导出, 此宏为空。
 */
#ifndef UNICONNECT_COMMON_H
#define UNICONNECT_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef UNI_SDK_BUILD
#ifdef _WIN32
#define UNI_API __declspec(dllexport)
#else
#define UNI_API __attribute__((visibility("default")))
#endif
#else
#ifdef _WIN32
#define UNI_API __declspec(dllimport)
#else
#define UNI_API
#endif
#endif

/* ABI 版本(查询函数与宏同步递增, 语义: 高16位=主版本不兼容, 低16位=增量) */
#define UNI_ABI_VERSION  1u

/* 通用错误码(两库共用区间; 各库扩展码从 0x1000 起) */
#define UNI_OK                0
#define UNI_ERR_PARAM        (-1)   /* 参数非法 */
#define UNI_ERR_NOMEM        (-2)   /* 库内资源分配失败 */
#define UNI_ERR_NOT_FOUND    (-3)   /* 设备未找到 */
#define UNI_ERR_BUSY         (-4)   /* 设备被占用 */
#define UNI_ERR_IO           (-5)   /* USB/传输 IO 错误 */
#define UNI_ERR_TIMEOUT      (-6)   /* 等待超时 */
#define UNI_ERR_NOT_SUP      (-7)   /* 当前平台/构建未实现 */
#define UNI_ERR_PROTO        (-8)   /* 设备协议应答错误 */
#define UNI_ERR_STATE        (-9)   /* 调用时序/状态错误 */
#define UNI_ERR_CRC          (-10)  /* 校验失败 */

/* USB 标识(VID/PID/接口/端点/Vendor0触发命令): 直引固件单一事实源头 */
#include "usb_ids.h"

#ifdef __cplusplus
}
#endif

#endif /* UNICONNECT_COMMON_H */
