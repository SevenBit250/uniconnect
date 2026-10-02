#include "bsp_led.h"
#include "ch32v30x.h"
#include <rtthread.h>

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

