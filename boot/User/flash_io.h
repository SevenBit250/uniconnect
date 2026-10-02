#ifndef __FLASH_IO_H
#define __FLASH_IO_H

#include <stdint.h>
#include "boot.h"

int flash_io_page_program(uint32_t addr, const uint8_t buf[256]);
void flash_io_read_flag(boot_flag_t *f);
int flash_io_write_flag(const boot_flag_t *f);

#endif /* __FLASH_IO_H */
