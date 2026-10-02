/* pin.h -- UniConnect 全板引脚单一事实源 (boot/APP 共用)
 *
 * 目标: 换板/改引脚只改这一个文件。各 bsp 驱动与 boot 一律 #include "pin.h"
 * 引用宏(common/ 在两侧工程的 -I 路径上), 不得私定义引脚。
 * 例外: DAP_config.h 的 SWD 位拍宏是性能路径直写端口寄存器, 保持独立实现,
 * 但引脚分配以本文件为准, 变更时须同步 DAP_config.h。
 * 硬件锚点: v1 板布线(2026-10-01/02 真机核对, 见 AGENTS 引脚条目)。 */
#ifndef __PIN_H
#define __PIN_H

#include "ch32v30x.h"

/* ---- 状态 LED ×5 (统一高电平为灭, 推挽输出) ---- */
#define PIN_LED_STA_PORT     GPIOB    /* 系统状态: APP=USB未连1s闪/已连常亮; boot升级态=500ms周期快闪 */
#define PIN_LED_STA_PIN      GPIO_Pin_11
#define PIN_LED_DAP_PORT     GPIOB    /* SWD/DAP 活动灯 */
#define PIN_LED_DAP_PIN      GPIO_Pin_10
#define PIN_LED_COM_PORT     GPIOC    /* CDC/串口活动灯 */
#define PIN_LED_COM_PIN      GPIO_Pin_4
#define PIN_LED_CAN1_PORT    GPIOA    /* CAN1 活动灯 */
#define PIN_LED_CAN1_PIN     GPIO_Pin_4
#define PIN_LED_CAN2_PORT    GPIOA    /* CAN2 活动灯 */
#define PIN_LED_CAN2_PIN     GPIO_Pin_3

/* ---- USART1 (CDC↔UART 桥, 默认映射) ---- */
#define PIN_USART1_TX_PORT   GPIOA
#define PIN_USART1_TX_PIN    GPIO_Pin_9
#define PIN_USART1_RX_PORT   GPIOA
#define PIN_USART1_RX_PIN    GPIO_Pin_10

/* ---- bxCAN ×2 ----
 * CAN1=默认映射(CAN_REMAP保持复位值0, 勿写Remap: 厂商库读改写会污染PCFR1
 * 其他位; Remap1=PB8/9是v1踩坑, 静默回环不驱动总线)
 * CAN2=固定映射(PB13/12, 无重映射选项) */
#define PIN_CAN1_TX_PORT     GPIOA
#define PIN_CAN1_TX_PIN      GPIO_Pin_12
#define PIN_CAN1_RX_PORT     GPIOA
#define PIN_CAN1_RX_PIN      GPIO_Pin_11
#define PIN_CAN2_TX_PORT     GPIOB
#define PIN_CAN2_TX_PIN      GPIO_Pin_13
#define PIN_CAN2_RX_PORT     GPIOB
#define PIN_CAN2_RX_PIN      GPIO_Pin_12

/* ---- SWD 调试引擎 (位拍实现在 DAP_config.h, 分配以此为准) ---- */
#define PIN_SWCLK_PORT       GPIOB    /* SWCLK, 与 SPI1-SCK 复用路线(v2 SPI加速预留) */
#define PIN_SWCLK_PIN        GPIO_Pin_3
#define PIN_SWDIO_PORT       GPIOB    /* SWDIO */
#define PIN_SWDIO_PIN        GPIO_Pin_5

#endif /* __PIN_H */
