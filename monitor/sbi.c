/*
 * The SBI: services the monitor offers to the normal-world kernel through ecall.
 *
 *   a7 = extension ID, a6 = function ID, a0-a5 = arguments
 *   returns a0 = error code, a1 = value
 *
 * Only what a single-hart Linux needs to boot is implemented, plus the secure call.
 */
#include <stdint.h>
#include "dtee.h"
#include "layout.h"
#include "riscv.h"
#include "sbi.h"
#include "uart.h"
#include "world.h"

#define SBI_SPEC_VERSION    (1UL << 24)     /* SBI v1.0 */
#define SBI_IMPL_ID         0x52565445UL    /* "RVTE": this monitor */

static void set_timer(uint64_t when)
{
    /* S-mode's timer compare register. Linux normally writes it directly (Sstc). */
    asm volatile ("csrw 0x14d, %0" :: "r"(when));
}

static void system_reset(uint32_t value)
{
    world_print_stats();
    *(volatile uint32_t *)SYSCON_BASE = value;
    for (;;)
        asm volatile ("wfi");
}

static int extension_present(uint64_t eid)
{
    switch (eid) {
    case SBI_EID_SET_TIMER:
    case SBI_EID_PUTCHAR:
    case SBI_EID_SHUTDOWN:
    case SBI_EID_BASE:
    case SBI_EID_TIME:
    case SBI_EID_IPI:
    case SBI_EID_RFENCE:
    case SBI_EID_SRST:
    case SBI_EID_DTEE:
        return 1;
    }
    return 0;
}

static long base_call(uint64_t fid, uint64_t arg0, long *val)
{
    switch (fid) {
    case 0: *val = SBI_SPEC_VERSION;            return SBI_SUCCESS;
    case 1: *val = SBI_IMPL_ID;                 return SBI_SUCCESS;
    case 2: *val = 1;                           return SBI_SUCCESS;   /* implementation version */
    case 3: *val = extension_present(arg0);     return SBI_SUCCESS;
    case 4: *val = (long)csr_read(mvendorid);   return SBI_SUCCESS;
    case 5: *val = (long)csr_read(marchid);     return SBI_SUCCESS;
    case 6: *val = (long)csr_read(mimpid);      return SBI_SUCCESS;
    }
    return SBI_ERR_NOT_SUPPORTED;
}

void sbi_handle(struct trap_frame *tf)
{
    uint64_t eid  = tf->regs[REG_A7];
    uint64_t fid  = tf->regs[REG_A6];
    uint64_t arg0 = tf->regs[REG_A0];
    long err = SBI_ERR_NOT_SUPPORTED;
    long val = 0;

    switch (eid) {
    /* ---- Secure call ---- */
    case SBI_EID_DTEE:
        if (fid == DTEE_FID_REGISTER)
            err = dtee_register(arg0);
        else if (fid == DTEE_FID_SUBMIT)
            err = dtee_submit(&val);
        else if (fid == DTEE_FID_MISSION)
            err = dtee_mission(&val);
        break;

    /* ---- Standard services ---- */
    case SBI_EID_BASE:
        err = base_call(fid, arg0, &val);
        break;

    case SBI_EID_PUTCHAR:
        uart_putc((char)arg0);
        err = SBI_SUCCESS;
        break;

    case SBI_EID_GETCHAR:
        err = -1;                               /* no character available */
        break;

    case SBI_EID_SET_TIMER:
    case SBI_EID_TIME:
        set_timer(arg0);
        err = SBI_SUCCESS;
        break;

    case SBI_EID_IPI:                           /* one hart: an IPI goes to ourselves */
        csr_set(mip, IP_SSIP);
        err = SBI_SUCCESS;
        break;

    case SBI_EID_RFENCE:                        /* one hart: a remote flush is a local flush */
        asm volatile ("sfence.vma");
        err = SBI_SUCCESS;
        break;

    case SBI_EID_SHUTDOWN:
        system_reset(SYSCON_POWEROFF);
        break;

    case SBI_EID_SRST:                          /* a0 = 0 shutdown, 1 or 2 = reboot */
        system_reset(arg0 == 0 ? SYSCON_POWEROFF : SYSCON_REBOOT);
        break;

    /* ---- Test only ---- */
    case SBI_EID_DEBUG:
        err = SBI_SUCCESS;
        if (fid == DEBUG_FID_ACCEPTED)
            val = (long)actuator_accepted_count();
        else if (fid == DEBUG_FID_FAILSAFE) {
            val = (long)actuator_failsafe_count();
            world_print_stats();
        } else if (fid == DEBUG_FID_PRINT_STATS)
            world_print_stats();
        else
            err = SBI_ERR_NOT_SUPPORTED;
        break;
    }

    tf->regs[REG_A0] = (uint64_t)err;
    tf->regs[REG_A1] = (uint64_t)val;
}
