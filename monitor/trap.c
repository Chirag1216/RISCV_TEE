#include <stdint.h>
#include "riscv.h"
#include "sbi.h"
#include "trap.h"
#include "uart.h"
#include "world.h"

extern char trap_vector[];       /* from trap.S */
extern char __trap_stack_top[];  /* from monitor.ld */

static const char *const exc_names[16] = {
    "instruction address misaligned",   /* 0  */
    "instruction access fault",         /* 1  */
    "illegal instruction",              /* 2  */
    "breakpoint",                       /* 3  */
    "load address misaligned",          /* 4  */
    "load access fault",                /* 5  */
    "store address misaligned",         /* 6  */
    "store access fault",               /* 7  */
    "ecall from U-mode",                /* 8  */
    "ecall from S-mode",                /* 9  */
    "reserved",                         /* 10 */
    "ecall from M-mode",                /* 11 */
    "instruction page fault",           /* 12 */
    "load page fault",                  /* 13 */
    "reserved",                         /* 14 */
    "store page fault",                 /* 15 */
};

static void print_csr(const char *name, uint64_t v)
{
    uart_puts("  ");
    uart_puts(name);
    uart_puts(" = ");
    uart_puthex(v);
    uart_puts("\n");
}

void trap_init(void)
{
    csr_write(mscratch, (uint64_t)__trap_stack_top);
    csr_write(mtvec, (uint64_t)trap_vector);   /* low bits 00 = direct mode */
}

void trap_handler(struct trap_frame *tf)
{
    uint64_t mcause  = csr_read(mcause);
    uint64_t mepc    = csr_read(mepc);
    uint64_t mtval   = csr_read(mtval);
    uint64_t code    = mcause & ~MCAUSE_INTERRUPT;
    int is_interrupt = (mcause & MCAUSE_INTERRUPT) != 0;
    int from_m       = (csr_read(mstatus) & MSTATUS_MPP_MASK) == MSTATUS_MPP_M;

    /* ---- Any trap while the verifier runs ends its run (does not return) ---- */
    if (in_trusted_world() && !from_m)
        trusted_trap(tf, mcause);

    /* ---- M-mode dead-man's timer expired while normal world was running ---- */
    if (!in_trusted_world() && is_interrupt && !from_m && code == IRQ_M_TIMER) {
        deadman_expired();
        return;
    }

    /* ---- Service request from the normal-world kernel ---- */
    if (!is_interrupt && !from_m && code == EXC_ECALL_S) {
        sbi_handle(tf);
        csr_write(mepc, csr_read(mepc) + 4);    /* ecall is always 4 bytes */
        return;
    }

    /* ---- Anything else is unexpected: report and stop ---- */
    uart_puts("\n[trap] ");
    if (is_interrupt) {
        uart_puts("interrupt");
    } else if (code < 16) {
        uart_puts(exc_names[code]);
    } else {
        uart_puts("unknown exception");
    }
    uart_puts(from_m ? " (from M-mode)\n" : " (from S/U-mode)\n");
    print_csr("mcause", mcause);
    print_csr("mepc  ", mepc);
    print_csr("mtval ", mtval);
    uart_puts("  -> fatal, halting\n");
    while (1) {
        asm volatile ("wfi");
    }
}
