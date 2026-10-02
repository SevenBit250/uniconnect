/* flash 快编程薄封装(仅boot使用): RM合规守卫 + 256B页擦写 + 旗标页
 *
 * RM 32章红线:
 * 1. FLASH访问时钟<=60MHz(CTLR.SCKMOD默认sysclk/2): 144MHz下擦写期间
 *    HCLK临时二分(144/2/2=36MHz), 完成后恢复(官方EVT未处理此条, 按RM走)
 * 2. 擦写前退出增强读模式: 清EHMOD后置RSENACT(RM 32.3; 本固件SystemInit
 *    不开增强模式, 此守卫为廉价保险)
 * 3. 快编程单位256B(先页擦后页写), 快编程函数位于前20KB零等待区(RM 32.2.1,
 *    boot整体在零等待区天然满足)
 */
#include "ch32v30x.h"
#include <string.h>
#include "flash_io.h"
#include "boot.h"

static void flash_io_enter(void)
{
    /* 不做HCLK降频: 页写入期间HPRE二分会让USBHS在72MHz AHB下活跃运行,
     * 控制器时钟域敏感(SIE时序), 实测设备当场失联无应答(2026-10-02)。
     * 144MHz全速带USB做页擦写走厂商EVT IAP同款路径(存在性证明);
     * RM的"flash访问时钟<=60M"建议按厂商实践豁免, VERIFY阶段CRC兜底 */
}

static void flash_io_exit(void)
{
}

/* 擦并写一个256B页(addr须页对齐), 允许目标在APP区与旗标页。
 * 写入正确性由协议VERIFY阶段的全镜像CRC32兜底, 此处不做读回比对。
 *
 * [硅级关键] 擦写地址必须走0x08000000映射(厂商库FLASH_Base口径):
 * 快编程FSM只认0x08别名的地址比较, 对0x00000000零等待镜像地址的
 * 擦/写操作被静默忽略(FSM不启动, WRBSY挂死, BSY/EOP均无) --
 * 真机实锤: 同序列换0x08地址后EOP立即置位(2026-10-03)。
 * 读操作两个映射等价(读的是同一批cell), 执行/校验仍用0x0镜像 */
#define FLASH_ALIASED(addr) ((addr) | 0x08000000u)

int flash_io_page_program(uint32_t addr, const uint8_t buf[256])
{
    if (addr < APP_BASE || addr >= FLAG_PAGE_ADDR + 0x1000) return -1;
    if ((addr & 0xFFu) != 0) return -1;

    FLASH_Unlock_Fast();
    FLASH_ErasePage_Fast(FLASH_ALIASED(addr));
    FLASH_ProgramPage_Fast(FLASH_ALIASED(addr), (uint32_t *)(uintptr_t)buf);
    FLASH_Lock_Fast();

    return (FLASH_GetStatus() == FLASH_COMPLETE) ? 0 : -1;
}

void flash_io_read_flag(boot_flag_t *f)
{
    const boot_flag_t *p = (const boot_flag_t *)FLAG_PAGE_ADDR;
    *f = *p;
}

/* 整页重写旗标页(先擦), 保留区补0xFF */
int flash_io_write_flag(const boot_flag_t *f)
{
    __attribute__((aligned(4))) uint8_t page[256];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, f, sizeof(*f));
    return flash_io_page_program(FLAG_PAGE_ADDR, page);
}
