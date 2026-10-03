/* can_proto.h -- CAN-USB 桥接线协议定义 (boot/APP/SDK/上位机单一事实源)
 *
 * host-safe 契约: 本文件只依赖 <stdint.h>, 必须保持可在宿主机(Windows/
 * Linux/macOS)编译 -- sdk/ 的构建就是对它的机械检测。
 * 来源: 2026-10-03 自 app/Bsp/device/can/bsp_can.h 抽取(纯搬移, 值逐项核对)。 */
#ifndef __CAN_PROTO_H
#define __CAN_PROTO_H

#include <stdint.h>

/* ---- CAN-USB 桥接协议 (WinUSB bulk, 单接口双路复用) ----
 * 包头8B(LE): [type:1][ch:1][seq:2][count:2][rsv:2], 记录4字节对齐 */
#define CANP_TX        0x01  /* H->D: count帧, 16B记录: id:4 flags:1 dlc:1 handle:2 data:8 */
#define CANP_CTRL      0x02  /* H->D: [cmd:1][payload...] */
#define CANP_SOFTRESET 0x7F  /* H->D: 链路软复位 */
#define CANP_RX        0x11  /* D->H: count帧, 20B记录: id:4 flags:1 dlc:1 rsv:1 ts:4 data:8 */
#define CANP_TXACK     0x13  /* D->H: count条, 4B记录: handle:2 status:1 ch:1 */
#define CANP_EVENT     0x14  /* D->H: count条, 8B记录: ev:1 rsv:1 value:4 rsv:2 */
#define CANP_CTRL_RSP  0x15  /* D->H: [cmd:1][status:1][payload...] */

/* CTRL命令 */
#define CANC_GET_INFO    0x01  /* rsp: proto:1 ch_num:1 rsv:2 */
#define CANC_SET_BITRATE 0x02  /* payload: u32 kbps */
#define CANC_SET_MODE    0x03  /* payload: u8 mode */
#define CANC_GET_STATUS  0x04  /* rsp: 每路tx/rx/err u32x3 */

/* 通道模式(wire值, 与固件bsp_can.h一致: 仅三种) */
#define CAN_MODE_LOOPBACK_TEST  0   /* 静默回环: 硬件回环+不驱动总线(自测) */
#define CAN_MODE_NORMAL         1   /* 正常收发(接总线) */
#define CAN_MODE_SLEEP          2   /* 停止(睡眠, 不收不发) */

/* 事件码 */
#define CANE_RX_OVF      0x01  /* value=累计丢弃帧数 */
#define CANE_BUS_OFF     0x02
#define CANE_ERR_PASSIVE 0x03

#define CAN_PROTO_VER    1

#endif /* __CAN_PROTO_H */
