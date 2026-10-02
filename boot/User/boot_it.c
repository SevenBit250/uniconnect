/* bootloader中断处理: 故障兜底, 其余走启动文件弱默认
 * (跳转APP已改为main上下文普通函数调用, 见main.c -- 软中断裸跳路径废弃) */
#include "ch32v30x.h"

/* 空软中断桩: 捕获后实测有野生Software_IRQn(14)挂起在entry处派发
 * (来源待查, mcause=0x8000000E实锚), 弱默认处理器行为未知,
 * 显示桩化后原样返回不再扰动(mepc=entry被劫持的环, 2026-10-02) */
void SW_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void SW_Handler(void)
{
}

void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void NMI_Handler(void)
{
    while (1) { }
}

void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void)
{
    while (1) { }
}
