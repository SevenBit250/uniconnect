/* boot_shared.h -- bootloader与APP的共享约定(单一事实源)
 *
 * BOINIT: RAM固定地址noinit共享区, boot与APP间的同步通道。
 *  - 软件复位(NVIC_SystemReset)不清SRAM, 旗标跨boot/APP存活
 *  - 上电(POR)后内容随机: 必须魔法字校验, 概率性误命中~1/2^32可接受
 *  - 掉电丢旗标 = 回正常启动(升级尚未开始, 安全)
 *  - APP侧只写RAM(零flash操作, 无自擦除死锁问题); flash旗标页
 *    (0x3F000)仅boot写入(APP_GOOD镜像记录)
 *
 * 两工程Link.ld各保留该区(MEMORY BOINIT), 变量两侧均以绝对地址访问。
 * 改布局需同步: 本文件 + Ld/Link.ld + boot/Link.ld + docs/bootloader-design.md */
#ifndef __BOOT_SHARED_H
#define __BOOT_SHARED_H

#include <stdint.h>

#define BOINIT_BASE        0x2000D000u
#define BOINIT_SIZE        0x1000u           /* 4KB */

/* magic 值 */
#define BOINIT_MAGIC_REQ   0x5AA55AA5u   /* APP->boot: 请求进升级模式 */
#define BOINIT_MAGIC_BOOT  0xB007B007u   /* boot->APP: 上次由boot跳入(预留) */

/* APP侧写入后软复位; boot消费(读后清magic) */
typedef struct {
    volatile uint32_t magic;
    volatile uint32_t arg0;    /* 预留: 命令/版本 */
    volatile uint32_t app_len; /* 预留: 镜像长度 */
    volatile uint32_t app_crc; /* 预留: 镜像CRC */
} boinit_t;

/* flash旗标页(0x3F000, 仅boot读写)的记录布局 */
#define FLAG_PAGE_ADDR     0x0003F000u
#define FLAG_APP_GOOD      0xC39D5AA5u   /* boot升级完成后写入: len+crc有效 */

/* APP镜像头魔法字(0x5000首4B): APP侧User/main.c以.appsig段放置, boot侧
 * app_sig_ok校验(boot.h引用本定义); 改值须同步fw_update侧无需感知(仅boot用) */
#define APP_SIG_MAGIC      0x5AA5C39Du

#endif /* __BOOT_SHARED_H */
