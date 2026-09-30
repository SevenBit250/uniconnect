/**
 * @file bsp_watchdog.c
 * @brief 内存金丝雀 + USBHS OUT DMA地址看门狗
 *
 * 背景: 存在未根除的RAM野写(对象: 线程TCB/黑匣子区/全局数据), RX已循环化、
 * TX为只读, 剩余可写内存的硬件只有USBHS OUT DMA(地址源: 驱动状态机).
 * 本看门狗在被保护地址布金丝雀, 并周期校验UEP4_RX_DMA指向; 命中立即把
 * 现场写入崩溃盒并死循环, 由OpenOCD读取定位.
 */
#include "ch32v30x.h"
#include <rtthread.h>
#include "bsp_watchdog.h"
#include "bsp_usb.h"

/* USBHS真实基址0x40023400(非0x40022000!), UEP4_RX_DMA偏移0x2C
 * (USBHSD_TypeDef: RX_DMA组从+0x20起, UEP4=+0x2C) -- 错误基址读到无关AHB外设
 * 垃圾值, 复合模式CDC武装后watchdog必误杀(0217全灭根因, 曾污染复合模式测试) */
#define USBHS_UEP4_RX_DMA (*(volatile uint32_t *)(0x40023400u + 0x2Cu))

/* 崩溃盒(与ch32v30x_it.c共用), [12..15]为看门狗槽 */
#define CRASH_BOX ((volatile uint32_t *)0x2000F000)

/* 金丝雀区: _ebss(约0x2000c000)与调试区之间的未用RAM */
#define CANARY_BASE 0x2000E800u
#define CANARY_COUNT 4u
#define CANARY_MAGIC 0xA5C39E7Bu

extern bsp_usb_cdc_buffer_t cdc_handle;   /* bsp_usb.c, 校验OUT DMA指向 */

static volatile uint32_t canary_addr[CANARY_COUNT];

static void watchdog_hit(uint32_t kind, uint32_t a, uint32_t b)
{
    /* 先关PMP再写黑匣子(黑匣子区已被PMP设为只读防CPU野写) */
    asm volatile("csrw 0x3A0, zero" ::: "memory");
    CRASH_BOX[12] = 0x7A0DC0DEu;
    CRASH_BOX[13] = kind;
    CRASH_BOX[14] = a;
    CRASH_BOX[15] = b;
    while (1);
}

static void canary_init(void)
{
    volatile uint32_t *p = (volatile uint32_t *)CANARY_BASE;
    for (uint32_t i = 0; i < CANARY_COUNT; i++) {
        uint32_t addr = CANARY_BASE + i * 256;
        canary_addr[i] = addr;
        volatile uint32_t *q = (volatile uint32_t *)addr;
        for (uint32_t j = 0; j < 16; j++) {
            q[j] = CANARY_MAGIC ^ (i * 31 + j);
        }
    }
    (void)p;
}

void bsp_watchdog_check(void)
{
    /* 1. 金丝雀校验 */
    for (uint32_t i = 0; i < CANARY_COUNT; i++) {
        volatile uint32_t *q = (volatile uint32_t *)canary_addr[i];
        for (uint32_t j = 0; j < 16; j++) {
            if (q[j] != (CANARY_MAGIC ^ (i * 31 + j))) {
                watchdog_hit(1, canary_addr[i] + j * 4, q[j]);
            }
        }
    }
    /* 2. USBHS OUT DMA指向校验: 必须落在CDC OUT池内(或0)。
     * 实测该寄存器回读丢bit29(RAM基址0x20000000不存储, 硬件隐含),
     * 两侧都掩掉bit29再比较 -- 曾用截断值比完整指针, CDC一武装必误杀
     * (黑匣子kind=2实锤: dma=0x00007b20在池内却判越界, monitor线程陪葬) */
    uint32_t dma = USBHS_UEP4_RX_DMA & 0x1FFFFFFFu;
    uint32_t base = (uint32_t)cdc_handle.rx_out_pool.storage & 0x1FFFFFFFu;
    if (dma != 0 && (dma < base || dma >= base + 4096)) {
        watchdog_hit(2, dma, base);
    }
    /* 3. 黑匣子看门狗槽被涂改检测(init时清零, 只有野写会改它) */
    if (CRASH_BOX[12] != 0 && CRASH_BOX[12] != 0x7A0DC0DEu) {
        watchdog_hit(3, CRASH_BOX[12], CRASH_BOX[13]);
    }
}

static int bsp_watchdog_init(void)
{
    canary_init();
    CRASH_BOX[0] = 0;    /* 清上次会话的黑匣子, 新捕获以magic为准 */
    CRASH_BOX[12] = 0;
    CRASH_BOX[13] = 0;
    CRASH_BOX[14] = 0;
    CRASH_BOX[15] = 0;
    /* PMP诱捕v2: 保护两个"绝对无人合法写入"的目标, CPU侧写者触发store fault
     * 进黑匣子(带PC); DMA写者不触发(由金丝雀轮询兜底).
     * entry0=诱饵区0x2000E000..E7FF只读(NAPOT 2KB): pmpaddr0=(base>>2)|(len/8-1)
     *          =0x8003800|0xFF=0x080038FF, cfg=0x1A
     *          (曾位于0xD000且常量与注释不一致, .bss增长被DAP缓冲埋掉后连锅端)
     * entry1=defunct链表头@0x20000518只读(NA4): 健康系统仅线程退出时写,
     *          本工程所有线程不退出, 任何写入=线程意外退出或野写
     * 布局纪律: .bss必须<0x2000E000(Link.ld有ASSERT), 0xE000起依次为
     * 诱饵区(2KB)/金丝雀(0xE800)/黑匣子(0xF000), 互不重叠
     * fault处理器先 csrw 0x3A0,zero 再写黑匣子(ch32v30x_it.c已含) */
    asm volatile(
        "csrw 0x3B0, %0\n\t"   /* pmpaddr0 = 0x2000E7FF>>2 */
        "csrw 0x3B1, %1\n\t"   /* pmpaddr1 = 0x20000518>>2 */
        "csrw 0x3A0, %2\n\t"   /* pmpcfg0 = entry1<<8|entry0 */
        :
        : "r"(0x080038FFu), "r"(0x08000146u), "r"(0x121Au)
        : "memory");
    return 0;
}
INIT_BOARD_EXPORT(bsp_watchdog_init);
