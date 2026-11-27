/*
 * Minimal device-tree editing: add entries to the memory reservation block.
 *
 * A flattened device tree is:  header | reservation block | structure | strings
 * The reservation block is a list of (address, size) pairs of 64-bit numbers,
 * ended by a (0, 0) pair. A kernel treats every listed range as "do not use".
 * To add a pair, everything after the start of the list moves up by 16 bytes.
 * All numbers in a device tree are big-endian.
 */
#include <stdint.h>
#include "fdt.h"

#define FDT_MAGIC           0xd00dfeedU
#define OFF_MAGIC           0
#define OFF_TOTALSIZE       4
#define OFF_DT_STRUCT       8
#define OFF_DT_STRINGS      12
#define OFF_MEM_RSVMAP      16

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void wr64(uint8_t *p, uint64_t v)
{
    wr32(p, (uint32_t)(v >> 32));
    wr32(p + 4, (uint32_t)v);
}

int fdt_reserve(void *dtb, uint64_t base, uint64_t size)
{
    uint8_t *fdt = dtb;

    if (rd32(fdt + OFF_MAGIC) != FDT_MAGIC)
        return -1;

    uint32_t totalsize = rd32(fdt + OFF_TOTALSIZE);
    uint32_t off_struct = rd32(fdt + OFF_DT_STRUCT);
    uint32_t off_strings = rd32(fdt + OFF_DT_STRINGS);
    uint32_t off_rsv = rd32(fdt + OFF_MEM_RSVMAP);

    /* Move everything from the start of the list up by one entry (copy from the end). */
    for (uint32_t i = totalsize; i > off_rsv; i--)
        fdt[i - 1 + 16] = fdt[i - 1];

    /* The new entry goes first in the list. */
    wr64(fdt + off_rsv, base);
    wr64(fdt + off_rsv + 8, size);

    /* The blocks behind the list have moved. */
    if (off_struct >= off_rsv)
        wr32(fdt + OFF_DT_STRUCT, off_struct + 16);
    if (off_strings >= off_rsv)
        wr32(fdt + OFF_DT_STRINGS, off_strings + 16);
    wr32(fdt + OFF_TOTALSIZE, totalsize + 16);

    return 0;
}
