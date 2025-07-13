#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "ch32v30x.h"

/* gpio def */
#define SWD_STA_Port GPIOB
#define SWD_STA_Pin GPIO_Pin_10
#define SYS_STA_Port GPIOB
#define SYS_STA_Pin GPIO_Pin_11
#define COM_STA_Port GPIOC
#define COM_STA_Pin GPIO_Pin_4
#define CAN1_STA_Port GPIOA
#define CAN1_STA_Pin GPIO_Pin_4
#define CAN2_STA_Port GPIOA
#define CAN2_STA_Pin GPIO_Pin_3

#ifdef __cplusplus
 }
#endif

#endif 