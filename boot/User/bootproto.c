/* HID升级协议状态机(主循环上下文执行, USB回调只做入队)
 * 帧格式与命令字见 boot.h(与 tools/fw_update.py 约定) */
#include <stdint.h>
#include <string.h>
#include "ch32v30x.h"
#include "boot.h"
#include "bootproto.h"
#include "flash_io.h"

/* CRC32 (reflected, poly 0xEDB88320, init 0xFFFFFFFF, xorout 0xFFFFFFFF) */
uint32_t boot_crc32(const uint8_t *p, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    while (len--) {
        crc ^= *p++;
        for (uint32_t k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

/* ---- 会话状态 ---- */
static struct {
    uint8_t  active;        /* BEGIN之后置位 */
    uint32_t img_len;       /* 镜像长度 */
    uint32_t img_crc;       /* 主机期望CRC32 */
    uint32_t wr_next;       /* 下一顺序写入地址(顺序编程约束) */
    uint8_t  page[256];     /* 页暂存(凑满256写) */
    uint32_t page_base;
    uint32_t page_len;
} s;

static uint8_t stage_push(const uint8_t *data, uint32_t len, uint32_t addr)
{
    /* 顺序写入: 必须紧跟上次末尾, 且不得跨界页中回绕 */
    if (addr != s.page_base + s.page_len) return 0;
    if (s.page_len + len > 256u) return 0;
    memcpy(&s.page[s.page_len], data, len);
    s.page_len += len;
    if (s.page_len == 256u) {
        if (flash_io_page_program(s.page_base, s.page) != 0) return 0;
        s.page_base += 256u;
        s.page_len = 0;
    }
    return 1;
}

void bootproto_reset(void)
{
    memset(&s, 0, sizeof(s));
}

/* REBOOT跳转挂起: 应答先回主机, 主循环发完应答再执行 */
static volatile uint8_t jump_pending;

void bootproto_request_jump(void)
{
    jump_pending = 1;
}

uint8_t bootproto_jump_pending(void)
{
    return jump_pending;
}

/* 处理一条主机请求 -> 填应答(64B), 返回应答长度(0=不应答) */
uint8_t bootproto_handle(const uint8_t *req, uint8_t *rsp)
{
    uint8_t cmd  = req[BP_OFF_CMD];
    uint8_t seq  = req[BP_OFF_SEQ];
    uint8_t plen = req[BP_OFF_LEN];
    uint32_t addr = req[BP_OFF_ADDR] | (req[BP_OFF_ADDR+1] << 8)
                  | ((uint32_t)req[BP_OFF_ADDR+2] << 16)
                  | ((uint32_t)req[BP_OFF_ADDR+3] << 24);
    const uint8_t *pl = &req[BP_OFF_PAYLOAD];
    uint8_t st = BPS_OK;

    memset(rsp, 0, HID_REPORT_SIZE);
    rsp[0] = cmd | 0x80u;
    rsp[1] = seq;

    switch (cmd) {
    case BPC_GET_INFO: {
        rsp[2] = BPS_OK;
        rsp[3] = BOOT_PROTO_VER;
        rsp[4] = (uint8_t)(BOOT_FLASH_SIZE >> 10);
        rsp[5] = (uint8_t)((BOOT_FLASH_SIZE >> 10) >> 8);
        rsp[7] = (uint8_t)APP_BASE;
        rsp[8] = (uint8_t)(APP_BASE >> 8);
        rsp[9] = (uint8_t)(APP_BASE >> 16);
        uint32_t m = APP_MAX_SIZE;
        rsp[11] = (uint8_t)m; rsp[12] = (uint8_t)(m >> 8); rsp[13] = (uint8_t)(m >> 16);
        return HID_REPORT_SIZE;
    }

    case BPC_BEGIN: {
        if (plen < 8) { st = BPS_ERR; break; }
        uint32_t len = pl[0] | (pl[1] << 8) | ((uint32_t)pl[2] << 16) | ((uint32_t)pl[3] << 24);
        uint32_t crc = pl[4] | (pl[5] << 8) | ((uint32_t)pl[6] << 16) | ((uint32_t)pl[7] << 24);
        if (addr != APP_BASE || len == 0 || len > APP_MAX_SIZE) { st = BPS_OVERFLOW; break; }
        bootproto_reset();
        s.active = 1;
        s.img_len = len;
        s.img_crc = crc;
        s.page_base = APP_BASE;
        break;
    }

    case BPC_PROGRAM: {
        if (!s.active) { st = BPS_STATE; break; }
        if (addr < APP_BASE || addr + plen > APP_BASE + s.img_len) { st = BPS_OVERFLOW; break; }
        if (s.page_base == APP_BASE && s.page_len == 0) {
            s.page_base = addr & ~0xFFu;          /* 镜像未页对齐时的首页 */
            if (addr != s.page_base) { st = BPS_ERR; break; }
        }
        if (!stage_push(pl, plen, addr)) { st = BPS_ERR; break; }
        break;
    }

    case BPC_FLUSH: {
        if (!s.active) { st = BPS_STATE; break; }
        if (s.page_len) {
            memset(&s.page[s.page_len], 0xFF, 256u - s.page_len);
            if (flash_io_page_program(s.page_base, s.page) != 0) { st = BPS_ERR; break; }
            s.page_base += 256u;
            s.page_len = 0;
        }
        break;
    }

    case BPC_VERIFY: {
        if (!s.active) { st = BPS_STATE; break; }
        uint32_t crc = boot_crc32((const uint8_t *)APP_BASE, s.img_len);
        if (crc != s.img_crc) { st = BPS_CRC; break; }
        break;
    }

    case BPC_END: {
        if (!s.active) { st = BPS_STATE; break; }
        boot_flag_t f = { FLAG_APP_GOOD, s.img_len, s.img_crc };
        if (flash_io_write_flag(&f) != 0) { st = BPS_ERR; break; }
        s.active = 0;
        break;
    }

    case BPC_REBOOT: {
        rsp[2] = BPS_OK;
        bootproto_request_jump();   /* 应答发出后由主循环执行 */
        return HID_REPORT_SIZE;
    }


    case BPC_ERASE_FLAG: {
        boot_flag_t f = { 0xFFFFFFFFu, 0, 0 };
        /* 全FF页写入即擦除效果: 复用write_flag */
        if (flash_io_write_flag(&f) != 0) { st = BPS_ERR; break; }
        break;
    }

    default:
        st = BPS_ERR;
        break;
    }

    rsp[2] = st;
    return HID_REPORT_SIZE;
}
