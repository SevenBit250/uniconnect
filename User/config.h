#ifndef __CONFIG_H
#define __CONFIG_H

/**
 * @brief 监视线程优先级和线程栈配置
 * 
 */
#define MONITOR_THREAD_PRIORITY             22

#define MONITOR_THREAD_STACK_SIZE           256u

/**
 * @brief CDC服务线程优先级和线程栈配置
 * 
 */
#define CDC_TX_THREAD_PRIORITY             10
#define CDC_RX_THREAD_PRIORITY             10
#define CDC_CTRL_THREAD_PRIORITY           9
#define CDC_STA_LED_THREAD_PRIORITY        21

/* 线程栈需容纳线程自身调用链 + 落在其上的中断C调用帧(-O0帧很大) */
#define CDC_TX_THREAD_STACK_SIZE           4096u
#define CDC_RX_THREAD_STACK_SIZE           4096u
#define CDC_CTRL_THREAD_STACK_SIZE         4096u
#define CDC_STA_LED_THREAD_STACK_SIZE      512u

/**
 * @brief DAP服务线程优先级和线程栈配置
 * 
 */
#define DAP_THREAD_PRIORITY        11

#define DAP_THREAD_STACK_SIZE      1024u

/**
 * @brief CAN回环自测线程(驱动起来阶段验收; 接USB桥后整体替换)
 */
#define CAN_TEST_THREAD_PRIORITY   20
#define CAN_TEST_THREAD_STACK_SIZE 512u

#endif
