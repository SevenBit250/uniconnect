#ifndef __CONFIG_H
#define __CONFIG_H

/**
 * @brief 监视线程优先级和线程栈配置
 * 
 */
#define MONITOR_THREAD_PRIORITY             22

/* monitor兼统一LED管理(看门狗+SYS心跳+吞吐活动灯+确认双闪), 调用链浅 */
#define MONITOR_THREAD_STACK_SIZE           1024u

/**
 * @brief CDC服务线程优先级和线程栈配置
 *
 */
#define CDC_TX_THREAD_PRIORITY             10
#define CDC_RX_THREAD_PRIORITY             10
#define CDC_CTRL_THREAD_PRIORITY           9

/* 线程栈需容纳线程自身调用链 + 落在其上的中断C调用帧(-O0帧很大)。
 * 2026-10-01栈整治: 4096(危机夜f978ad9恐慌值)按实测高水位收敛至1024。
 * 依据5M四工况soak实测(tx/rx/ctrl=508/508/240B, 轮次间有±50B抖动,
 * 768时rx曾达66%利用率越报警线, 1024=2x实测最坏值)。含中断帧。
 * 验证手段: tools/stack_watermark.py('#'预填栈水位单调保持, 改栈后必复测) */
#define CDC_TX_THREAD_STACK_SIZE           1024u
#define CDC_RX_THREAD_STACK_SIZE           1024u
#define CDC_CTRL_THREAD_STACK_SIZE         1024u

/**
 * @brief DAP服务线程优先级和线程栈配置
 * 
 */
#define DAP_THREAD_PRIORITY        11

/* 2026-10-01 SWD全功能压测(枚举/halt/寄存器/断点观察点/flash擦写回环)实测
 * 高水位672B, 1024时利用率65%越报警线, SWD深化还会再深, 提至1536 */
#define DAP_THREAD_STACK_SIZE      1536u

/**
 * @brief CAN-USB桥接线程(协议引擎: 块池拼装/解析, 双路复用)
 */
#define CAN_BRIDGE_THREAD_PRIORITY   5
#define CAN_BRIDGE_THREAD_STACK_SIZE 2048u

#endif
