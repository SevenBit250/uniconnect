#ifndef __BSP_LED_H
#define __BSP_LED_H

#include "stdint.h"

#ifdef __cplusplus
 extern "C" {
#endif

#define BSP_LED_ID_SYS_STA  0
#define BSP_LED_ID_SWD_STA  1
#define BSP_LED_ID_COM_STA  2
#define BSP_LED_ID_CAN1_STA 3
#define BSP_LED_ID_CAN2_STA 4

void bsp_led_ctrl(uint8_t id, uint8_t onoff);

void bsp_led_ctrl_bit(uint8_t onoff);

void bsp_led_toggle(uint8_t id);


#ifdef __cplusplus
 }
#endif

#endif 