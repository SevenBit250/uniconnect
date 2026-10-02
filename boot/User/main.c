/* UniConnect Bootloader 主逻辑
 *
 * 启动决策(docs/bootloader-design.md):
 *   旗标=UPGRADE_REQ(APP请求) 或 APP无效 -> 升级模式(HID, PID 0x0219)
 *   否则(旗标GOOD且CRC过 / 旗标空且APP签名在) -> 跳APP(0x5004)
 *
 * 跳转走EVT IAP同款软中断路径: Software_IRQn pending -> SW_Handler裸跳,
 * 借"WCH-Interrupt-fast"中断上下文保证关中断的干净起点(APP启动代码
 * 自行重设mtvec/mstatus, 参见Startup handle_reset)。 */
#include "ch32v30x.h"
#include "ch32v30x_usb.h"
#include "boot.h"
#include "bootproto.h"
#include "flash_io.h"
#include <stdint.h>

void boot_usb_init(void);
void boot_usb_pump(void);

/* 启动阶段检查点(裸地址SRAM, openocd读): RISC-V向量模式异常走mtvec[0]=_start
 * 静默重启启动链(无芯片复位/无日志), 用它定位异常循环的深度。
 * 地址选0xC000(APP bss顶0xBB04之上、PMP诱饵区0xE000之下的无人区) --
 * 0xF800曾在APP栈区被涂改, 0xE700踩PMP诱饵(锁定PMP跨异常环存活,
 * M态写也陷阱, 曾致boot自查写即崩的自持循环, 2026-10-02实锚) */
#define BOOT_STAGE_ADDR ((volatile uint32_t *)0x2000C000u)
#define BOOT_STAGE(v) do { BOOT_STAGE_ADDR[0] = 0xB00B0000u | (v); } while (0)
/* main入口抢救上一圈的异常现场(异常循环时mcause/mepc为APP崩溃点, 且
 * SystemInit不触碰这些CSR, 到main仍新鲜) */
static void boot_capture_trap(void)
{
    uint32_t mc, mp, mv;
    __asm volatile("csrr %0, mcause" : "=r"(mc));
    __asm volatile("csrr %0, mepc"   : "=r"(mp));
    __asm volatile("csrr %0, mtval"  : "=r"(mv));
    BOOT_STAGE_ADDR[1] = mc;
    BOOT_STAGE_ADDR[2] = mp;
    BOOT_STAGE_ADDR[3] = mv;
}

/* PMP全清: 异常/跳转回boot不是芯片复位, APP配置的锁定PMP(诱饵区M态陷阱)
 * 会跨圈存活, 不清则boot任何写入诱饵区的代码自陷(2026-10-02实锚) */
static void pmp_scrub(void)
{
    __asm volatile(
        "csrw pmpcfg0, zero\n"
        "csrw pmpcfg1, zero\n"
        "csrw pmpcfg2, zero\n"
        "csrw pmpcfg3, zero\n"
        ::: "memory");
}

/* startup的handle_reset跳转目标(RT-Thread工程由components.c提供) */
extern int main(void);
void entry(void) { main(); }

/* ---- LED: 仅STA灯(PB11, 高电平为灭, 同bsp_led) ---- */
#define LED_STA_PORT   GPIOB
#define LED_STA_PIN    GPIO_Pin_11

static void led_init(void)
{
    GPIO_InitTypeDef g = {0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_SetBits(LED_STA_PORT, LED_STA_PIN);
    g.GPIO_Pin = LED_STA_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_STA_PORT, &g);
}

static void delay_ms(uint32_t ms)
{
    /* 粗延时(volatile循环), 仅LED节奏用; 144MHz下约8 cyc/迭代 */
    while (ms--) {
        for (volatile uint32_t i = 0; i < 16000; i++) {
            __asm volatile("nop");
        }
    }
}

/* ---- APP有效性 ---- */

static uint8_t app_sig_ok(void)
{
    return *(volatile uint32_t *)APP_BASE == APP_SIG_MAGIC;
}

static uint8_t app_crc_ok(const boot_flag_t *f)
{
    return boot_crc32((const uint8_t *)APP_BASE, f->app_len) == f->app_crc;
}

static void pfic_scrub(void);

/* ---- 跳转APP ----
 * 普通函数指针跳转(非EVT的软中断裸跳): 软中断路径留下"活动未mret"的中断
 * 上下文, APP侧RT-Thread再开中断即异常弹回mtvec[0]=_start形成静默重启循环
 * (2026-10-02真机实锤: 检查点A2已写而pc回到0x0; openocd直跳0x5004则APP正常)。
 * boot main为纯净上下文(USB已停+中断源已关), 平凡调用即安全 */
static void jump_to_app(void)
{
    void (*app_entry)(void) = (void (*)(void))(APP_BASE + 4);

    /* USB彻底回复位态(IAP_2_APP同款序列), 断D+上拉让主机看到重枚举 */
    USBHSD->HOST_CTRL = 0x00;
    USBHSD->CONTROL = 0x00;
    USBHSD->INT_EN = 0x00;
    USBHSD->ENDP_CONFIG = 0xFFFFFFFF;
    USBHSD->CONTROL &= ~USBHS_UC_DEV_PU_EN;
    USBHSD->CONTROL |= USBHS_UC_CLR_ALL | USBHS_UC_RESET_SIE;
    USBHSD->CONTROL = 0x00;
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, DISABLE);

    NVIC_DisableIRQ(USBHS_IRQn);
    delay_ms(20);

    pfic_scrub();                         /* 送APP一个干净NVIC(跳转非复位) */
    BOOT_STAGE(0xA1);
    __asm volatile("fence.i" ::: "memory");
    app_entry();
    BOOT_STAGE(0xAF);                      /* 不应到达 */
    while (1) { }
}

/* PFIC彻底清洗(禁全部+清挂起): 异常/跳转回boot不是芯片复位, 上一场固件
 * (APP)留下的NVIC使能+挂起会全程存活 -- 曾实测USBHS_IRQn(85)挂起在
 * handle_reset的mret进entry瞬间劫持到mtvec+4*85=随机代码, 形成静默
 * 死循环(2026-10-02, mcause=0x80000055@mepc=entry实锚) */
static void pfic_scrub(void)
{
    for (uint32_t i = 0; i < 8; i++) {
        NVIC->IRER[i] = 0xFFFFFFFFu;   /* 先禁全部中断源 */
        NVIC->IPRR[i] = 0xFFFFFFFFu;   /* 再清全部挂起 */
    }
}

int main(void)
{
    boot_flag_t flag;
    uint8_t upgrade = 0;
    uint32_t tick = 0;

    pmp_scrub();
    pfic_scrub();
    boot_capture_trap();
    SystemCoreClockUpdate();
    led_init();

    /* BOINIT(软复位存活的RAM共享区): APP升级请求(读后即消费) */
    volatile boinit_t *bi = (volatile boinit_t *)BOINIT_BASE;
    flash_io_read_flag(&flag);

    if (bi->magic == BOINIT_MAGIC_REQ) {
        bi->magic = 0;
        upgrade = 1;                       /* APP显式请求 */
    } else if (flag.magic == FLAG_APP_GOOD) {
        if (!app_sig_ok() || !app_crc_ok(&flag)) {
            upgrade = 1;                   /* 记录在但镜像坏 */
        }
    } else {
        /* 无GOOD记录(openocd直烧APP的首刷场景): 仅凭签名放行 */
        if (!app_sig_ok()) {
            upgrade = 1;
        }
    }

    if (!upgrade) {
        jump_to_app();                     /* 不返回 */
    }

    /* ---- 升级模式: STA灯快闪(5Hz) ---- */
    /* 裸机必须显式开全局中断: 启动代码mret进main时MPIE=0即MIE关(APP工程
     * 靠RT-Thread线程切换恢复MIE), 不开则USB中断全灭、枚举停摆(2026-10-02
     * 真机实锤: USBHS已初始化而主机枚举不出设备) */
    __enable_irq();
    bootproto_reset();
    boot_usb_init();

    while (1) {
        boot_usb_pump();
        if (bootproto_jump_pending()) {
            /* 升级收尾走芯片复位而非跳转: 复位后boot按GOOD记录+签名正常
             * 引导APP, APP从POR干净态启动 -- 跳转入口的寄存器/RAM残留
             * 曾致CDC线控(EP0带数据阶段请求)时好时坏(2026-10-03实锚) */
            delay_ms(50);                  /* 让REBOOT应答先到主机 */
            NVIC_SystemReset();
        }
        if (++tick >= 20000) {             /* 与pump节拍叠加, 约5Hz */
            tick = 0;
            GPIO_WriteBit(LED_STA_PORT, LED_STA_PIN,
                          (BitAction)!GPIO_ReadOutputDataBit(LED_STA_PORT, LED_STA_PIN));
        }
    }
}
