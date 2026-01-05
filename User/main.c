/********************************** (C) COPYRIGHT *******************************
* File Name          : main.c
* Author             : WCH
* Version            : V1.0.0
* Date               : 2021/06/06
* Description        : Main program body.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/

/*
 *@Note
 USART Print debugging routine:
 USART1_Tx(PA9).
 This example demonstrates using USART1(PA9) as a print debug port output.

*/
#include "main.h"
#include "debug.h"
#include <rtthread.h>

/* Global typedef */

/* Global define */

/* Global Variable */


/*********************************************************************
 * @fn      main
 *
 * @brief   Main program.
 *
 * @return  none
 */
int main(void)
{
	while(1) {
		rt_thread_mdelay(1000);
		GPIO_WriteBit(SYS_STA_Port, SYS_STA_Pin, !GPIO_ReadOutputDataBit(SYS_STA_Port, SYS_STA_Pin));
	}
}

void usb_dc_low_level_init(void)
{
    RCC_USBCLK48MConfig(RCC_USBCLK48MCLKSource_USBPHY);
    RCC_USBHSPLLCLKConfig(RCC_HSBHSPLLCLKSource_HSE);
    RCC_USBHSConfig(RCC_USBPLL_Div2);
    RCC_USBHSPLLCKREFCLKConfig( RCC_USBHSPLLCKREFCLK_4M);
    RCC_USBHSPHYPLLALIVEcmd(ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, ENABLE);
    NVIC_EnableIRQ(USBHS_IRQn);
}

BOARD_INIT(device_init)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC, ENABLE);
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Pin = SWD_STA_Pin;
	GPIO_SetBits(SWD_STA_Port, SWD_STA_Pin);
    GPIO_Init(SWD_STA_Port, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = SYS_STA_Pin;
	GPIO_SetBits(SYS_STA_Port, SYS_STA_Pin);
    GPIO_Init(SYS_STA_Port, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = COM_STA_Pin;
	GPIO_SetBits(COM_STA_Port, COM_STA_Pin);
    GPIO_Init(COM_STA_Port, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = CAN1_STA_Pin;
	GPIO_SetBits(CAN1_STA_Port, CAN1_STA_Pin);
    GPIO_Init(CAN1_STA_Port, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = CAN2_STA_Pin;
	GPIO_SetBits(CAN2_STA_Port, CAN2_STA_Pin);
    GPIO_Init(CAN2_STA_Port, &GPIO_InitStructure);

    usb_dc_low_level_init();
    return 0;
}

