#include <stdint.h>
#include "fdt.h"
#include "layout.h"
#include "pmp.h"
#include "riscv.h"
#include "trap.h"
#include "uart.h"
#include "world.h"

void enter_normal_world(uint64_t entry, uint64_t hartid, uint64_t dtb);   /* enter.S */

/* Exceptions the normal-world kernel handles itself, without the monitor. */
#define DELEGATED_EXCEPTIONS (                                              \
    (1UL << EXC_INSN_MISALIGNED)   | (1UL << EXC_INSN_ACCESS_FAULT)  |      \
    (1UL << EXC_ILLEGAL_INSN)      | (1UL << EXC_BREAKPOINT)         |      \
    (1UL << EXC_LOAD_MISALIGNED)   | (1UL << EXC_LOAD_ACCESS_FAULT)  |      \
    (1UL << EXC_STORE_MISALIGNED)  | (1UL << EXC_STORE_ACCESS_FAULT) |      \
    (1UL << EXC_ECALL_U)           | (1UL << EXC_INSN_PAGE_FAULT)    |      \
    (1UL << EXC_LOAD_PAGE_FAULT)   | (1UL << EXC_STORE_PAGE_FAULT))

/* Set up what the normal world is allowed to do on its own. */
static void normal_world_init(void)
{
    /* Its own exceptions and interrupts go straight to S-mode.
     * A PMP violation is one of them: the kernel gets an access fault. */
    csr_write(medeleg, DELEGATED_EXCEPTIONS);
    csr_write(mideleg, IP_SSIP | IP_STIP | IP_SEIP);

    csr_write(satp, 0);                 /* no address translation until the kernel sets it up */
    csr_write(mcounteren, 7);           /* S-mode may read the cycle, time and instruction counters */
    csr_write(scounteren, 7);           /* and so may U-mode: Linux programs read the time directly */

    /* Give S-mode its own timer (stimecmp) and the cache-block instructions. */
    asm volatile ("csrw 0x30a, %0" :: "r"(MENVCFG_STCE | MENVCFG_PBMTE | MENVCFG_CBZE |
                                          MENVCFG_CBCFE | MENVCFG_CBIE));
    asm volatile ("csrw 0x14d, %0" :: "r"(~0UL));       /* stimecmp: no timer interrupt pending */

    /* mret will return to S-mode, with interrupts off. */
    uint64_t mstatus = csr_read(mstatus);
    mstatus &= ~(MSTATUS_MPP_MASK | MSTATUS_MPIE);
    mstatus |= MSTATUS_MPP_S;
    csr_write(mstatus, mstatus);
}

void main(unsigned long dtb)
{
    trap_init();                 /* first: from here on, a fault prints instead of hanging */

    uart_puts("monitor: up in M-mode\n");

    pmp_probe();
    world_init();                /* copy the verifier into trusted memory */
    pmp_init();                  /* normal view: monitor and trusted regions are off limits */

    /* Tell the normal-world kernel which memory is not its own. */
    if (fdt_reserve((void *)dtb, MON_BASE, NORMAL_BASE - MON_BASE) != 0 ||
        fdt_reserve((void *)dtb, TRUSTED_BASE, TRUSTED_END - TRUSTED_BASE) != 0)
        uart_puts("monitor: WARNING: could not patch the device tree\n");

    normal_world_init();

    uart_puts("monitor: entering normal world (S-mode) at ");
    uart_puthex(NORMAL_BASE);
    uart_puts("\n\n");

    enter_normal_world(NORMAL_BASE, 0, dtb);
}
