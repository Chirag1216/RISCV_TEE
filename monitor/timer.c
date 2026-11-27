#include <stdint.h>
#include "layout.h"
#include "riscv.h"
#include "timer.h"

#define MTIME       (*(volatile uint64_t *)CLINT_MTIME)
#define MTIMECMP    (*(volatile uint64_t *)CLINT_MTIMECMP)

#define TICKS_PER_US    (TIMEBASE_HZ / 1000000)

uint64_t time_now_us(void)
{
    return MTIME / TICKS_PER_US;
}

/*
 * The machine timer interrupt cannot be masked by S-mode or U-mode code,
 * so the monitor always regains control when the budget runs out.
 */
void watchdog_arm(uint64_t budget_us)
{
    MTIMECMP = MTIME + budget_us * TICKS_PER_US;
    csr_set(mie, MIE_MTIE);
}

void watchdog_disarm(void)
{
    csr_clear(mie, MIE_MTIE);
    MTIMECMP = ~0UL;            /* also clears a pending timer interrupt */
}
