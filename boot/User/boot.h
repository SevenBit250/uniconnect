/* bootloader 公共定义: 分区地址/旗标页布局/升级协议
 * 与 docs/bootloader-design.md、tools/fw_update.py、APP侧(dap触发)三方约定,
 * 改任一字段三处同步 */
#ifndef __BOOT_H
#define __BOOT_H

#include <stdint.h>
#include "boot_shared.h"          /* BOINIT共享区/flash旗标页布局(单一事实源), common/经-I解析 */

/* ---- flash 分区 (CODE 256KB) ---- */
#define BOOT_FLASH_BASE      0x00000000u
#define BOOT_FLASH_SIZE      (256u * 1024u)
#define APP_BASE             0x00005000u          /* APP起始(.appsig魔法字在此, 代码入口+4) */
#define APP_MAX_SIZE         (BOOT_FLASH_SIZE - 0x1000u - APP_BASE)  /* 228KB, 尾部留旗标页 */

/* APP镜像头(0x5000)魔法字: 定义已收敛至boot_shared.h(单一事实源) */

/* flash旗标页记录(仅boot写: 升级完成后的镜像有效性记录) */
typedef struct {
    uint32_t magic;      /* FLAG_APP_GOOD 或 擦除态 */
    uint32_t app_len;    /* 镜像长度(字节, APP代码区起算) */
    uint32_t app_crc;    /* 镜像CRC32(长度覆盖app_len) */
} boot_flag_t;

/* ---- HID 升级协议 (64B报告, 无report id) ---- */
#define HID_REPORT_SIZE      64u
/* 主机->设备: [cmd:1][seq:1][len:1][addr:4][payload:56] */
#define BP_OFF_CMD     0
#define BP_OFF_SEQ     1
#define BP_OFF_LEN     2   /* payload长度 0..56 */
#define BP_OFF_ADDR    3
#define BP_OFF_PAYLOAD 7
#define BP_PAYLOAD_MAX 56

#define BPC_GET_INFO   0x01   /* -> [proto:1][flash_kb:2][app_base:4][max_img:4] */
#define BPC_BEGIN      0x02   /* addr=APP_BASE, payload=[len:4][crc:4] */
#define BPC_PROGRAM    0x03   /* addr=目标地址, payload=数据(页缓冲凑满256即写) */
#define BPC_FLUSH      0x04   /* 尾页补0xFF写出 */
#define BPC_VERIFY     0x05   /* boot全APP区CRC32与BEGIN.crc比对 */
#define BPC_END        0x06   /* 校验通过: 写FLAG_APP_GOOD记录 */
#define BPC_REBOOT     0x07   /* 跳APP */
#define BPC_ERASE_FLAG 0x08   /* 擦旗标页(调试用) */

/* 应答 status */
#define BPS_OK         0x00
#define BPS_ERR        0x01   /* 通用失败(闪存操作/参数) */
#define BPS_STATE      0x02   /* 命令顺序错(未BEGIN就PROGRAM等) */
#define BPS_CRC        0x03   /* 校验失败 */
#define BPS_OVERFLOW   0x04   /* 镜像超长 */

#define BOOT_PROTO_VER 1u

#endif /* __BOOT_H */
