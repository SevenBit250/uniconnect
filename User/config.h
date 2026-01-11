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

#define CDC_TX_THREAD_STACK_SIZE           1024u
#define CDC_RX_THREAD_STACK_SIZE           1024u
#define CDC_CTRL_THREAD_STACK_SIZE         512u
#define CDC_STA_LED_THREAD_STACK_SIZE      256u

#endif
