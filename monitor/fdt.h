#ifndef FDT_H
#define FDT_H

#include <stdint.h>

/*
 * Mark [base, base + size) as reserved in the device tree at `dtb`, so the
 * normal-world kernel never allocates that memory. Returns 0 on success.
 */
int fdt_reserve(void *dtb, uint64_t base, uint64_t size);

#endif
