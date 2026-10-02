#include "ch32v30x.h"
#include <rtthread.h>   /* INIT_COMPONENT_EXPORT所在, 曾漏include导致自启动
                         * 注册被隐式声明吞掉, SPI从未初始化(ack恒0的根因) */
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
        /* SPI1 Remap: PCFR1 bit0, 独立的引脚复用选择, 与SWJ_CFG无关
         * (注意: 勿在此处做SWJ_Disable -- 曾误加, boot期执行即切断SDI
         * 烧录调试口, wch-link失联, 且PB3/PB4本就无需释放) */
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

    /* 注意: 不做SWJ_Disable! CH32V305实测PB3/PB4默认即可作GPIO/SPI复用
     * (GPIO里程碑从未释放JTAG也正常工作), 而SWJ_Disable会切断两线SDI
     * 烧录调试口(wch-link立即失联, 2026-10-01实锤) */
}

/* 开机自启: 引擎硬件必须先于任何主机命令就绪 -- 曾只挂DAP_SETUP命令,
 * 而openocd连接流程不发DAP_SETUP, SPI从未初始化(未使能外设TXE恒置位,
 * 移位循环静默空转零时钟, 现象=ack恒0连不上) */
static int bsp_dap_boot_init(void)
{
    bsp_dap_init();
    return 0;
}
INIT_COMPONENT_EXPORT(bsp_dap_boot_init);

void bsp_dap_swd_setup(void)
{
#if BSP_DAP_USE_SPI
    /* SPI模式: 引脚归SPI复用所有, 此处不可改GPIO模式(会把脚从AF抢走) */
    SPI_Cmd(SPI1, ENABLE);
#else
    /* SWD模式: CLK/DIO推挽输出高 */
    GPIO_SetBits(GPIOB, DAP_SWCLK_PIN | DAP_SWDIO_PIN);
    GPIOB->CFGLR = (GPIOB->CFGLR & ~(DIO_FIELD_MASK | (0xFu << 12))) |
                   DIO_FIELD_OUT_PP | (0x3u << 12);
#endif
}

void bsp_dap_off(void)
{
#if BSP_DAP_USE_SPI
    /* SPI模式: 停钟即可, Tx方向+CPOL0使SCK/SWDIO空闲低/高 */
    SPI_Cmd(SPI1, DISABLE);
#else
    /* High-Z: 浮空输入 */
    GPIOB->CFGLR = (GPIOB->CFGLR & ~(DIO_FIELD_MASK | (0xFu << 12))) |
                   DIO_FIELD_IN_FLT | (0x4u << 12);
#endif
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
#if !BSP_DAP_USE_SPI
    GPIOB->CFGLR = (GPIOB->CFGLR & ~DIO_FIELD_MASK) | DIO_FIELD_OUT_PP;
#endif
    /* SPI模式: 方向归BIDIOE管, DAP.c的SWD_Sequence/Pins处理器会调本函数
     * 操纵引脚模式, 若放行会把PB5从SPI复用抢走(实测连不上的根因) */
}

void bsp_dap_dio_out_disable(void)
{
#if !BSP_DAP_USE_SPI
    GPIOB->CFGLR = (GPIOB->CFGLR & ~DIO_FIELD_MASK) | DIO_FIELD_IN_FLT;
#endif
}

/***************************************************************************/
/*                                                                         */
/*              SPI兼容后端(字节级原语, 供加速SWD引擎)                       */
/*                                                                         */
/***************************************************************************/

#if BSP_DAP_USE_SPI

#include "DAP_config.h"   /* DAP.h要求先于其包含(定义DAP_SWD等) */
#include "DAP.h"          /* DAP_Data(强符号覆盖官方__WEAK协议引擎需要) */

/* 引擎观测(dap_thread.c的g_dap_dbg复用):
 * [0]=SWJ_Sequence调用数 [1]=SWD_Transfer调用数 [2]=最近ack [3]=最近Rx首字节 */
extern volatile uint32_t g_dap_dbg[4];

#endif /* BSP_DAP_USE_SPI */

/* 主机请求时钟(DAP_SWJ_Clock钩子调用): SPI引擎分频跟随, GPIO引擎忽略 */
uint32_t bsp_dap_spi_set_clock(uint32_t hz)
{
#if BSP_DAP_USE_SPI
    /* APB2=72MHz, 2^n分频, 上限36MHz */
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
#else
    (void)hz;
    return 0;
#endif
}

#if BSP_DAP_USE_SPI

/* 位反转表: SPI硬件MSB先移, SWD线序LSB先, 一切缓冲以线序存储、移位前反转 */
static const uint8_t rev8[16] = {0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE,
                                 0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF};
#define REV(x) ((uint8_t)((rev8[(x) & 0xF] << 4) | rev8[(x) >> 4]))

/* 线序缓冲取位: buf已还原为线序(LSB先), 取start位起的n位(自然LSB先) */
static uint32_t getbits(const uint8_t *buf, uint32_t start, uint32_t n)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; i++) {
        v |= (uint32_t)((buf[(start + i) >> 3] >> ((start + i) & 7)) & 1) << i;
    }
    return v;
}

static void setbit(uint8_t *buf, uint32_t pos, uint32_t bit)
{
    if (bit) {
        buf[pos >> 3] |= (uint8_t)(1u << (pos & 7));
    }
}

void bsp_dap_spi_write(const uint8_t *buf, uint32_t n)
{
    SPI_BiDirectionalLineConfig(SPI1, SPI_Direction_Tx);
    while (n--) {
        while (SPI1->STATR & SPI_STATR_BSY) {}
        while (!(SPI1->STATR & SPI_STATR_TXE)) {}
        SPI1->DATAR = REV(*buf);
        buf++;
    }
    while (SPI1->STATR & SPI_STATR_BSY) {}
}

void bsp_dap_spi_read(uint8_t *buf, uint32_t n)
{
    /* SPE逐字节门控: 保证恰好8个时钟/字节(规避单线Rx模式主机连续时钟
     * 行为的不确定性, 时钟间隙SWD容忍) */
    SPI1->CTLR1 &= ~SPI_CTLR1_SPE;
    SPI_BiDirectionalLineConfig(SPI1, SPI_Direction_Rx);
    while (n--) {
        (void)SPI1->DATAR;                       /* 清残余RXNE */
        SPI1->CTLR1 |= SPI_CTLR1_SPE;            /* 放出8个时钟 */
        while (!(SPI1->STATR & SPI_STATR_RXNE)) {}
        SPI1->CTLR1 &= ~SPI_CTLR1_SPE;           /* 立即停钟 */
        *buf = REV((uint8_t)SPI1->DATAR);
        buf++;
    }
    SPI_BiDirectionalLineConfig(SPI1, SPI_Direction_Tx);
    SPI1->CTLR1 |= SPI_CTLR1_SPE;                /* 恢复Tx使能 */
}

/***************************************************************************/
/*                                                                         */
/*    强符号覆盖官方__WEAK协议引擎: SPI字节级加速实现                         */
/*                                                                         */
/*  线上协议逐位不变, SPI只是把每8个连续线位打包成一个移位字节, 相位边界      */
/*  用位偏移切片; 字节对齐自然产生的尾部空闲时钟落在包间/换向段(规范容忍)。    */
/*  事务线序: [req 8clk主驱][ta释放][ack 3目标][data 33 主或目标][尾闲]      */
/*                                                                         */
/***************************************************************************/

/* SWJ位流输出(线复位/JTAG-SWD切换码): count位(1..256), LSB先, 尾部补1对齐 */
void SWJ_Sequence(uint32_t count, const uint8_t *data)
{
    uint8_t buf[33];
    uint32_t nbytes = (count + 7) >> 3;
    g_dap_dbg[0]++;
    for (uint32_t i = 0; i < nbytes; i++) {
        buf[i] = 0xFF;   /* 先全1: 尾部pad与未覆盖位安全 */
    }
    for (uint32_t i = 0; i < count; i++) {
        setbit(buf, i, (data[i >> 3] >> (i & 7)) & 1);
    }
    bsp_dap_spi_write(buf, nbytes);
}

/* SWD段序列(DAP_SWD_Sequence): info[6:0]=位数(1..64), bit7=DIN方向 */
void SWD_Sequence(uint32_t info, const uint8_t *swdo, uint8_t *swdi)
{
    uint32_t count = info & 0x7F;
    uint32_t nbytes = (count + 7) >> 3;
    if (info & 0x80) {
        uint8_t buf[8];
        bsp_dap_spi_read(buf, nbytes);
        for (uint32_t i = 0; i < nbytes; i++) {
            swdi[i] = buf[i];
        }
    } else {
        uint8_t buf[8];
        for (uint32_t i = 0; i < nbytes; i++) {
            buf[i] = 0xFF;
        }
        for (uint32_t i = 0; i < count; i++) {
            /* 写0位需清位 */
            if (!((swdo[i >> 3] >> (i & 7)) & 1)) {
                buf[i >> 3] &= (uint8_t)~(1u << (i & 7));
            }
        }
        bsp_dap_spi_write(buf, nbytes);
    }
}

/* SWD事务: request=A[3:2]RnW APnDP(bits[3:0]), 返回ACK[2:0] */
uint8_t SWD_Transfer(uint32_t request, uint32_t *data)
{
    uint32_t ta = DAP_Data.swd_conf.turnaround;
    uint8_t apn = request & 1u, rnw = (request >> 1) & 1u;
    uint8_t a2 = (request >> 2) & 1u, a3 = (request >> 3) & 1u;
    uint8_t par = (uint8_t)(apn ^ rnw ^ a2 ^ a3);
    /* 线序请求字节: Start=1,APnDP,RnW,A2,A3,Par,Stop=0,Park=1 */
    uint8_t req = (uint8_t)(1u | (apn << 1) | (rnw << 2) | (a2 << 3) |
                            (a3 << 4) | (par << 5) | (1u << 7));
    uint8_t ack;

    g_dap_dbg[1]++;
    bsp_dap_spi_write(&req, 1);

    if (rnw) {
        /* 读: 5字节Rx = ta + ack3 + data32 + par1 + 尾闲 */
        uint8_t in[5];
        bsp_dap_spi_read(in, 5);
        g_dap_dbg[3] = in[0];
        ack = (uint8_t)getbits(in, ta, 3);
        g_dap_dbg[2] = ack;
        if (ack == DAP_TRANSFER_OK) {
            uint32_t v = getbits(in, ta + 3, 32);
            uint32_t p = getbits(in, ta + 3 + 32, 1);
            uint32_t chk = v ^ (v >> 16);
            chk ^= chk >> 8; chk ^= chk >> 4; chk ^= chk >> 2; chk ^= chk >> 1;
            if ((chk & 1u) != p) {
                ack = DAP_TRANSFER_ERROR;
            }
            if (data) {
                *data = v;
            }
        }
    } else {
        /* 写: 1字节Rx = ta + ack3 + 尾闲(目标释放段), 取ack决定是否驱动数据 */
        uint8_t in[1];
        bsp_dap_spi_read(in, 1);
        ack = (uint8_t)getbits(in, ta, 3);
        if (ack == DAP_TRANSFER_OK) {
            /* 5字节Tx = data32(LSB先) + parity + 7个尾闲1 */
            uint8_t out[5];
            uint32_t v = data ? *data : 0;
            uint32_t chk = v ^ (v >> 16);
            chk ^= chk >> 8; chk ^= chk >> 4; chk ^= chk >> 2; chk ^= chk >> 1;
            for (uint32_t i = 0; i < 5; i++) {
                out[i] = 0xFF;
            }
            for (uint32_t i = 0; i < 32; i++) {
                if (!((v >> i) & 1u)) {
                    out[i >> 3] &= (uint8_t)~(1u << (i & 7));
                }
            }
            if (!(chk & 1u)) {
                out[4] &= (uint8_t)~(1u << 0);   /* bit32=parity */
            }
            bsp_dap_spi_write(out, 5);
        } else {
            /* NAK: 不驱动数据, 5字节Rx当空闲时钟 */
            uint8_t in2[5];
            bsp_dap_spi_read(in2, 5);
        }
    }
    return ack;
}

#endif /* BSP_DAP_USE_SPI */
