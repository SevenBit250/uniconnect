#include "bsp_led.h"
#include "ch32v30x.h"
#include "pin.h"     /* 全板引脚单一事实源(common/), 本文件不再私定义 */
#include <rtthread.h>

/* 引脚宏迁移: 原私有宏 -> pin.h 统一命名(值不变) */
#define SWD_STA_Port  PIN_LED_DAP_PORT
#define SWD_STA_Pin   PIN_LED_DAP_PIN
#define SYS_STA_Port  PIN_LED_STA_PORT
#define SYS_STA_Pin   PIN_LED_STA_PIN
#define COM_STA_Port  PIN_LED_COM_PORT
#define COM_STA_Pin   PIN_LED_COM_PIN
#define CAN1_STA_Port PIN_LED_CAN1_PORT
#define CAN1_STA_Pin  PIN_LED_CAN1_PIN
#define CAN2_STA_Port PIN_LED_CAN2_PORT
#define CAN2_STA_Pin  PIN_LED_CAN2_PIN

struct led_group_typedef
{
    GPIO_TypeDef *port;
    uint16_t pin;
}
const static led_group[] = 
{
    {SYS_STA_Port, SYS_STA_Pin},
    {SWD_STA_Port, SWD_STA_Pin},
    {COM_STA_Port, COM_STA_Pin},
    {CAN1_STA_Port, CAN1_STA_Pin},
    {CAN2_STA_Port, CAN2_STA_Pin},
};

void bsp_led_ctrl_bit(uint8_t onoff)
{
    GPIO_WriteBit(SYS_STA_Port, SYS_STA_Pin,   !(onoff & 0b00001));
    GPIO_WriteBit(SWD_STA_Port, SWD_STA_Pin,   !(onoff & 0b00010));
    GPIO_WriteBit(COM_STA_Port, COM_STA_Pin,   !(onoff & 0b00100));
    GPIO_WriteBit(CAN1_STA_Port, CAN1_STA_Pin, !(onoff & 0b01000));
    GPIO_WriteBit(CAN2_STA_Port, CAN2_STA_Pin, !(onoff & 0b10000));
}

void bsp_led_ctrl(uint8_t id, uint8_t onoff)
{
    GPIO_WriteBit(led_group[id].port, led_group[id].pin, !onoff);
}

void bsp_led_toggle(uint8_t id) {
    uint8_t sta = GPIO_ReadOutputDataBit(led_group[id].port, led_group[id].pin);
    GPIO_WriteBit(led_group[id].port, led_group[id].pin, !sta);
}

static int bsp_led_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC, ENABLE);
    bsp_led_ctrl_bit(0b00000);
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Pin = SWD_STA_Pin;
    GPIO_Init(SWD_STA_Port, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = SYS_STA_Pin;
    GPIO_Init(SYS_STA_Port, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = COM_STA_Pin;
    GPIO_Init(COM_STA_Port, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = CAN1_STA_Pin;
    GPIO_Init(CAN1_STA_Port, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = CAN2_STA_Pin;
    GPIO_Init(CAN2_STA_Port, &GPIO_InitStructure);
    return 0;
}
INIT_BOARD_EXPORT(bsp_led_init);

