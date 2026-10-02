#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "ch32v30x.h"

typedef enum{
    SYS_STA_USB_NOT_CONNECTED,
    SYS_STA_USB_CONNECTED,
}system_status_t;

/* 功能灯open标志: 数据面写, monitor线程读(LED渲染集中于此, 数据路径零GPIO调用)
 * open语义: COM=主机DTR(SET_CONTROL_LINE_STATE), DAP=HostStatus(debugger connected);
 * CAN1/2不走此结构 -- 灯态直接取bsp_can_get_mode(硬件通道open, SET_MODE驱动) */
typedef struct {
    volatile uint8_t com;      /* CDC主机串口open(DTR) */
    volatile uint8_t dap_conn; /* DAP调试器会话open(OpenOCD等) */
    volatile uint8_t dap_run;  /* DAP目标运行态(预留, 暂不渲染) */
} led_open_t;
extern led_open_t g_led_open;

void updateSystemStatus(system_status_t sta);

system_status_t getSystemStatus(void);

#ifdef __cplusplus
 }
#endif

#endif 