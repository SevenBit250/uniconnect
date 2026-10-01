#include "ch32v30x.h"
#include "bsp_config.h"
#include "bsp_dap.h"

/* SWD引脚引擎实现
 *
 * GPIO后端热路径直接操作BSHR/BCR/INDR/CFGLR(不走库函数调用, -O0下
 * 函数调用开销可观); PB3/PB5都在CRL(CFGLR低半字), 方向切换只动PB5的
 * 模式字段[23:20]: 0x3=推挽输出50M, 0x4=浮空输入。
 * SPI后端按EVT 1Lines_half-duplex例程的模式初始化, 方向切换用
 * SPI_BiDirectionalLineConfig。 */

/* PB5字段值与掩码 */
#define DIO_FIELD_MASK   (0xFu << 20)
#define DIO_FIELD_OUT_PP (0x3u << 20)   /* 推挽输出50MHz: CNF=00 MODE=11 */
#define DIO_FIELD_IN_FLT (0x4u << 20)   /* 浮空输入: CNF=01 MODE=00 */

void bsp_dap_init(void)
{
    GPIO_InitTypeDef g = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    /* PB3/PB4复用为JTDO/NJTRST, 释放为普通GPIO(EVT PWR例程参考) */
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);

    /* 初值先高再改模式, 避免毛刺 */
    GPIO_SetBits(GPIOB, DAP_SWCLK_PIN | DAP_SWDIO_PIN);
    g.GPIO_Pin = DAP_SWCLK_PIN | DAP_SWDIO_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &g);

#if BSP_DAP_USE_SPI
    {
        SPI_InitTypeDef s = {0};
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);
        /* SPI1 Remap: SCK=PB3, MOSI=PB5(单线模式即数据线) */
        GPIO_PinRemapConfig(GPIO_Remap_SPI1, ENABLE);
        g.GPIO_Pin = DAP_SWCLK_PIN | DAP_SWDIO_PIN;
        g.GPIO_Mode = GPIO_Mode_AF_PP;
        GPIO_Init(GPIOB, &g);
        s.SPI_Direction = SPI_Direction_1Line_Tx;
        s.SPI_Mode = SPI_Mode_Master;
        s.SPI_DataSize = SPI_DataSize_8b;
        /* SWD在SWCLK上升沿采样: CPOL低+第一边沿(=上升沿)采样 */
        s.SPI_CPOL = SPI_CPOL_Low;
        s.SPI_CPHA = SPI_CPHA_1Edge;
        s.SPI_NSS = SPI_NSS_Soft;
        s.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_64;  /* 72M/64≈1.1M */
        s.SPI_FirstBit = SPI_FirstBit_MSB;
        s.SPI_CRCPolynomial = 7;
        SPI_Init(SPI1, &s);
        SPI_Cmd(SPI1, ENABLE);
    }
#endif
}

void bsp_dap_swd_setup(void)
{
    /* SWD模式: CLK/DIO推挽输出高 */
    GPIO_SetBits(GPIOB, DAP_SWCLK_PIN | DAP_SWDIO_PIN);
    GPIOB->CFGLR = (GPIOB->CFGLR & ~(DIO_FIELD_MASK | (0xFu << 12))) |
                   DIO_FIELD_OUT_PP | (0x3u << 12);
}

void bsp_dap_off(void)
{
    /* High-Z: 浮空输入 */
    GPIOB->CFGLR = (GPIOB->CFGLR & ~(DIO_FIELD_MASK | (0xFu << 12))) |
                   DIO_FIELD_IN_FLT | (0x4u << 12);
}

void bsp_dap_clk_set(void)
{
    GPIOB->BSHR = DAP_SWCLK_PIN;
}

void bsp_dap_clk_clr(void)
{
    GPIOB->BCR = DAP_SWCLK_PIN;
}

void bsp_dap_dio_set(void)
{
    GPIOB->BSHR = DAP_SWDIO_PIN;
}

void bsp_dap_dio_clr(void)
{
    GPIOB->BCR = DAP_SWDIO_PIN;
}

void bsp_dap_dio_out(uint32_t bit)
{
    if (bit & 1u) {
        GPIOB->BSHR = DAP_SWDIO_PIN;
    } else {
        GPIOB->BCR = DAP_SWDIO_PIN;
    }
}

uint32_t bsp_dap_dio_in(void)
{
    return (GPIOB->INDR & DAP_SWDIO_PIN) ? 1u : 0u;
}

void bsp_dap_dio_out_enable(void)
{
    GPIOB->CFGLR = (GPIOB->CFGLR & ~DIO_FIELD_MASK) | DIO_FIELD_OUT_PP;
}

void bsp_dap_dio_out_disable(void)
{
    GPIOB->CFGLR = (GPIOB->CFGLR & ~DIO_FIELD_MASK) | DIO_FIELD_IN_FLT;
}

/***************************************************************************/
/*                                                                         */
/*              SPI兼容后端(字节级原语, 供将来的加速SWD引擎)                  */
/*                                                                         */
/***************************************************************************/

#if BSP_DAP_USE_SPI

uint32_t bsp_dap_spi_set_clock(uint32_t hz)
{
    /* APB2=72MHz, 2^n分频 */
    const uint16_t table[] = {SPI_BaudRatePrescaler_2, SPI_BaudRatePrescaler_4,
                              SPI_BaudRatePrescaler_8, SPI_BaudRatePrescaler_16,
                              SPI_BaudRatePrescaler_32, SPI_BaudRatePrescaler_64,
                              SPI_BaudRatePrescaler_128, SPI_BaudRatePrescaler_256};
    uint32_t div = (72000000u + hz - 1u) / hz;
    uint32_t i = 0, v = 2;
    while (i < 7 && v < div) {
        i++;
        v <<= 1;
    }
    SPI1->CTLR1 &= ~SPI_CTLR1_BR;
    SPI1->CTLR1 |= table[i] << 3;
    return 72000000u / v;
}

void bsp_dap_spi_write(const uint8_t *buf, uint32_t n)
{
    SPI_BiDirectionalLineConfig(SPI1, SPI_Direction_Tx);
    while (n--) {
        /* SPI硬件MSB先移, SWD线序LSB先: 位反转(查表) */
        static const uint8_t rev[16] = {0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE,
                                        0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF};
        uint8_t b = (rev[*buf & 0xF] << 4) | rev[*buf >> 4];
        while (SPI1->STATR & SPI_STATR_BSY) {}
        while (!(SPI1->STATR & SPI_STATR_TXE)) {}
        SPI1->DATAR = b;
        buf++;
    }
    while (SPI1->STATR & SPI_STATR_BSY) {}
}

void bsp_dap_spi_read(uint8_t *buf, uint32_t n)
{
    SPI_BiDirectionalLineConfig(SPI1, SPI_Direction_Rx);
    /* 1Line_Rx模式: 时钟由读触发(EVT例程: 先发后收, Rx侧等RXNE) */
    while (n--) {
        while (!(SPI1->STATR & SPI_STATR_RXNE)) {}
        uint8_t b = (uint8_t)SPI1->DATAR;
        static const uint8_t rev[16] = {0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE,
                                        0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF};
        *buf = (rev[b & 0xF] << 4) | rev[b >> 4];
        buf++;
    }
    /* 收完恢复发送方向(下次write再切), 时钟停在空闲低 */
    SPI_BiDirectionalLineConfig(SPI1, SPI_Direction_Tx);
}

#endif /* BSP_DAP_USE_SPI */
