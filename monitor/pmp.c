#include <stdint.h>
#include "layout.h"
#include "riscv.h"
#include "pmp.h"
#include "uart.h"

#define PMP_MAX 16

/* CSR names must be literal in the instruction, so each entry needs its own case. */
#define PMPADDR_CASE(n) case n: if (write) csr_write(pmpaddr##n, val); return csr_read(pmpaddr##n);

static uint64_t pmpaddr_rw(int idx, int write, uint64_t val)
{
    switch (idx) {
    PMPADDR_CASE(0)  PMPADDR_CASE(1)  PMPADDR_CASE(2)  PMPADDR_CASE(3)
    PMPADDR_CASE(4)  PMPADDR_CASE(5)  PMPADDR_CASE(6)  PMPADDR_CASE(7)
    PMPADDR_CASE(8)  PMPADDR_CASE(9)  PMPADDR_CASE(10) PMPADDR_CASE(11)
    PMPADDR_CASE(12) PMPADDR_CASE(13) PMPADDR_CASE(14) PMPADDR_CASE(15)
    }
    return 0;
}

/*
 * An unimplemented pmpaddr register reads back 0 whatever is written.
 * Write all ones, read back, then restore 0.
 * Entries are all OFF (pmpcfg = 0) at reset, so this changes no protection.
 */
int pmp_probe(void)
{
    int count = 0;
    uint64_t readback0 = 0;

    for (int i = 0; i < PMP_MAX; i++) {
        uint64_t v = pmpaddr_rw(i, 1, ~0UL);
        pmpaddr_rw(i, 1, 0);
        if (v == 0)
            break;
        if (i == 0)
            readback0 = v;
        count++;
    }

    uart_puts("pmp    : ");
    uart_puthex((uint64_t)count);
    uart_puts(" entries\n");

    if (count > 0) {
        /* Lowest writable address bit gives the granularity: 2^(bit+2) bytes. */
        int bit = 0;
        while (((readback0 >> bit) & 1) == 0)
            bit++;
        uart_puts("pmp granularity (bytes): ");
        uart_puthex(1UL << (bit + 2));
        uart_puts("\n");
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Region layout                                                       */
/* ------------------------------------------------------------------ */

/* pmpcfg bits for one entry */
#define PMP_R       0x01
#define PMP_W       0x02
#define PMP_X       0x04
#define PMP_NAPOT   0x18        /* address mode: naturally aligned power-of-two region */

/*
 * NAPOT encoding: for a region of `size` bytes (a power of two) at `base`
 * (a multiple of size), pmpaddr = (base | (size/2 - 1)) >> 2.
 */
#define NAPOT(base, size)   (((base) | (((size) >> 1) - 1)) >> 2)

/*
 * Entry numbers. The lowest-numbered matching entry wins, so the specific
 * regions come first and the catch-all comes last.
 *
 * None of the entries is locked, so M-mode (the monitor) is never restricted.
 * They apply to S-mode and U-mode only.
 */
#define ENT_MONITOR 0
#define ENT_TCODE   1
#define ENT_TDATA   2
#define ENT_ACT     3
#define ENT_ALL     4

/* pmpcfg0 holds entries 0-7, eight bits each. */
#define CFG(entry, bits)    ((uint64_t)(bits) << ((entry) * 8))

void pmp_init(void)
{
    csr_write(pmpaddr0, NAPOT(MON_BASE,   MON_SIZE));
    csr_write(pmpaddr1, NAPOT(TCODE_BASE, TCODE_SIZE));
    csr_write(pmpaddr2, NAPOT(TDATA_BASE, TDATA_SIZE));
    csr_write(pmpaddr3, NAPOT(ACT_BASE,   ACT_SIZE));
    csr_write(pmpaddr4, ~0UL);          /* all ones = the whole address space */

    pmp_set_view(PMP_VIEW_NORMAL);
}

void pmp_set_view(enum pmp_view view)
{
    uint64_t cfg;

    if (view == PMP_VIEW_NORMAL) {
        cfg = CFG(ENT_MONITOR, PMP_NAPOT)                               /* no access */
            | CFG(ENT_TCODE,   PMP_NAPOT)                               /* no access */
            | CFG(ENT_TDATA,   PMP_NAPOT)                               /* no access */
            | CFG(ENT_ACT,     PMP_NAPOT)                               /* no access */
            | CFG(ENT_ALL,     PMP_NAPOT | PMP_R | PMP_W | PMP_X);      /* the rest  */
    } else {
        cfg = CFG(ENT_MONITOR, PMP_NAPOT)                               /* no access */
            | CFG(ENT_TCODE,   PMP_NAPOT | PMP_R | PMP_X)
            | CFG(ENT_TDATA,   PMP_NAPOT | PMP_R | PMP_W)
            | CFG(ENT_ACT,     PMP_NAPOT | PMP_R | PMP_W)
            | CFG(ENT_ALL,     PMP_NAPOT);                              /* no access */
    }

    csr_write(pmpcfg0, cfg);
    /* Flush cached translations/permissions so the new view applies immediately. */
    asm volatile ("sfence.vma");
}
