#ifndef __BOOTPROTO_H
#define __BOOTPROTO_H

#include <stdint.h>
#include "boot.h"

uint32_t boot_crc32(const uint8_t *p, uint32_t len);
void bootproto_reset(void);
uint8_t bootproto_handle(const uint8_t *req, uint8_t *rsp);  /* 返回应答长度 */

/* REBOOT命令置位, main循环检查后执行跳转(保证应答先发出) */
void bootproto_request_jump(void);
uint8_t bootproto_jump_pending(void);

#endif /* __BOOTPROTO_H */
