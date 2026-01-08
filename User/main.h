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

void updateSystemStatus(system_status_t sta);

system_status_t getSystemStatus(void);

#ifdef __cplusplus
 }
#endif

#endif 