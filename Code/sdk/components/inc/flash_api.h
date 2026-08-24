/* All rights reserved.
 *
 */

#ifndef _FLASH_API_H_
#define _FLASH_API_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "osi_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long long    UINT64;

/*
*the flash address don't bigger the FACTORY address(0x00FD0000).
*/
uint32_t flash_erase(uint32_t flash_addr, uint32_t size);

int flash_erase_4k(uint32_t flash_addr, uint32_t size);

uint32_t flash_read(uint32_t flash_addr, uint32_t buf_addr, uint32_t size);

uint32_t flash_write(uint32_t flash_addr, uint32_t buf_addr, uint32_t size);

UINT64 flash_read_uid(void);

/*the ptable name must the aboot tool partition
 * if no find the ptable name will return 0*/
uint32_t flash_read_addr(const char *ptable_name);

uint32_t flash_read_size(const char *ptable_name);

/*used to operate external flash, only ZX800SG support*/
int extflash_spi_init(uint8_t sspport, uint8_t spi_clock);
uint32_t extflash_erase(uint8_t sspport, uint32_t flash_addr, uint32_t size);
uint32_t extflash_read(uint8_t sspport, uint32_t flash_addr, uint32_t buf_addr, uint32_t size);
uint32_t extflash_write(uint8_t sspport, uint32_t flash_addr, uint32_t buf_addr, uint32_t size);
#ifdef __cplusplus
}
#endif
#endif/*_FLASH_API_H_*/
