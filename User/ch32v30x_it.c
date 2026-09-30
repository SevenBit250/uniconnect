/********************************** (C) COPYRIGHT *******************************
* File Name          : ch32v30x_it.c
* Author             : WCH
* Version            : V1.0.0
* Date               : 2024/03/06
* Description        : Main Interrupt Service Routines.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#include "ch32v30x_it.h"
#include <rtthread.h>
#include "bsp.h"

void NMI_Handler(void) __attribute__((naked));
void HardFault_Handler(void) __attribute__((naked));

/*********************************************************************
 * 崩溃黑匣子: naked汇编把异常时刻的完整现场写入RAM不被初始化的区域
 * (0x2000E000, 堆/栈之间), 死循环保留现场由OpenOCD读取.
 * RISC-V异常入口不改写GPR, 因此这里的ra/sp就是故障现场的值.
 * 布局: [0]=magic [1]=tag [2]=mcause [3]=mepc [4]=mtval
 *       [5]=ra(肇事返回地址) [6]=sp(故障栈指针) [7]=HPE标志
 */
#define CRASH_MAGIC 0xC0DE5AEDu

#define CRASH_ASM(tag)                              \
    "li t4, 0x2000E000\n"                           \
    "csrr t0, mcause\n"                             \
    "csrr t1, mepc\n"                               \
    "csrr t2, mtval\n"                              \
    "li t3, " tag "\n"                              \
    "sw t3, 4(t4)\n"                                \
    "sw t0, 8(t4)\n"                                \
    "sw t1, 12(t4)\n"                               \
    "sw t2, 16(t4)\n"                               \
    "sw ra, 20(t4)\n"                               \
    "sw sp, 24(t4)\n"                               \
    "li t5, 0xC0DE5AED\n"                           \
    "sw t5, 0(t4)\n"                                \
    "1: j 1b\n"

/*********************************************************************
 * @fn      NMI_Handler
 *
 * @brief   This function handles NMI exception.
 *
 * @return  none
 */
void NMI_Handler(void)
{
    __asm volatile(CRASH_ASM("0x4E"));
}

/*********************************************************************
 * @fn      HardFault_Handler
 *
 * @brief   This function handles Hard Fault exception.
 *
 * @return  none
 */
void HardFault_Handler(void)
{
    __asm volatile(CRASH_ASM("0x48"));
}


