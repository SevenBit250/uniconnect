#ifndef __BSP_DAP_H
#define __BSP_DAP_H

#include <stdint.h>

/* CMSIS-DAP SWD引脚引擎 (Bsp/device/dap, 2026-10-01)
 *
 * 引脚: SWCLK=PB3, SWDIO=PB5 (定义在bsp_config.h)
 *  - PB3/PB4出厂复用为JTDO/NJTRST, init时GPIO_Remap_SWJ_Disable释放
 *    (EVT PWR/Sleep_Mode例程参考; wch-link走SDI口不受影响)
 *  - PB3/PB5 = SPI1 Remap后的SCK/MOSI, 两后端共用同一组引脚
 *
 * 两个后端(bsp_config.h的BSP_DAP_USE_SPI切换):
 *  - GPIO位拍(默认): 逐位原语, 官方SW_DP.c协议引擎直接驱动, 本阶段主力
 *  - SPI兼容: 字节级移位原语(bsp_dap_spi_*), 单线半双工BIDIMODE在PB5上
 *    双向收发(EVT EXAM/SPI/1Lines_half-duplex参考), 供将来重写SWD引擎
 *    做批量加速用; SPI硬件MSB先移而SWD线序LSB先, 移位前需软件位反转
 */

/* ---- 公共(两后端通用) ---- */
void bsp_dap_init(void);          /* 时钟+JTAG释放+引脚初值(输出高) */
void bsp_dap_swd_setup(void);     /* SWD模式: CLK/DIO推挽输出高 */
void bsp_dap_off(void);           /* 全部引脚浮空输入(High-Z) */

/* ---- GPIO位拍后端(逐位, 官方SW_DP.c协议引擎驱动) ---- */
void     bsp_dap_clk_set(void);
void     bsp_dap_clk_clr(void);
void     bsp_dap_dio_set(void);
void     bsp_dap_dio_clr(void);
void     bsp_dap_dio_out(uint32_t bit);
uint32_t bsp_dap_dio_in(void);
void     bsp_dap_dio_out_enable(void);    /* DIO切输出 */
void     bsp_dap_dio_out_disable(void);   /* DIO切输入(浮空) */

/* ---- SPI兼容后端(字节级原语, 供加速SWD引擎) ---- */
void     bsp_dap_spi_write(const uint8_t *buf, uint32_t n);  /* n字节移出, 每字节8个SWCLK */
void     bsp_dap_spi_read(uint8_t *buf, uint32_t n);         /* 方向切换后n字节移入 */
uint32_t bsp_dap_spi_set_clock(uint32_t hz);                 /* 返回实际频率 */

#endif /* __BSP_DAP_H */
