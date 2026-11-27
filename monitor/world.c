/*
 * The trusted world: loading the verifier, and the secure call that runs it.
 */
#include <stddef.h>
#include <stdint.h>
#include "dtee.h"
#include "layout.h"
#include "pmp.h"
#include "riscv.h"
#include "timer.h"
#include "uart.h"
#include "world.h"

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);

/* switch.S */
long run_verifier(uint64_t entry, uint64_t arg);
void verifier_return(long verdict) __attribute__((noreturn));

/* verifier_blob.S */
extern const char verifier_blob_start[], verifier_blob_end[];

#define VERIFIER_BUDGET_US  5000    /* the verifier must finish within 5 ms */
#define DEADMAN_TIMEOUT_US  500000  /* 500 ms without an accepted flight command -> failsafe */

static volatile struct actuator *const act = (volatile struct actuator *)ACT_BASE;

static uint64_t shared_pa;          /* 0 = not registered yet */
static int trusted;                 /* 1 while the verifier is running */
static int deadman_active;          /* 1 while armed in MODE_FLIGHT */
static uint64_t deadman_deadline_us;

struct lat_stats {
    uint64_t count;
    uint64_t min_cyc;
    uint64_t max_cyc;
    uint64_t sum_cyc;
    uint64_t min_ticks;
    uint64_t max_ticks;
    uint64_t sum_ticks;
};

static struct lat_stats pose_stats;
static struct lat_stats mission_stats;

static void stats_record(struct lat_stats *st,
                         uint64_t start_cyc, uint64_t start_ticks,
                         uint64_t end_cyc, uint64_t end_ticks)
{
    uint64_t dcyc   = end_cyc - start_cyc;
    uint64_t dticks = end_ticks - start_ticks;

    if (st->count == 0 || dcyc < st->min_cyc)
        st->min_cyc = dcyc;
    if (dcyc > st->max_cyc)
        st->max_cyc = dcyc;
    st->sum_cyc += dcyc;

    if (st->count == 0 || dticks < st->min_ticks)
        st->min_ticks = dticks;
    if (dticks > st->max_ticks)
        st->max_ticks = dticks;
    st->sum_ticks += dticks;

    st->count++;
}

static void print_one_stats(const char *kind, const struct lat_stats *st)
{
    if (st->count == 0)
        return;
    uart_puts("MONITOR_STATS,");
    uart_puts(kind);
    uart_puts(",count=");
    uart_putdec(st->count);
    uart_puts(",min_cyc=");
    uart_putdec(st->min_cyc);
    uart_puts(",avg_cyc=");
    uart_putdec(st->sum_cyc / st->count);
    uart_puts(",max_cyc=");
    uart_putdec(st->max_cyc);
    uart_puts(",min_ticks=");
    uart_putdec(st->min_ticks);
    uart_puts(",avg_ticks=");
    uart_putdec(st->sum_ticks / st->count);
    uart_puts(",max_ticks=");
    uart_putdec(st->max_ticks);
    uart_puts("\n");
}

void world_print_stats(void)
{
    print_one_stats("pose", &pose_stats);
    print_one_stats("mission", &mission_stats);
}

int in_trusted_world(void)
{
    return trusted;
}

uint64_t actuator_accepted_count(void) { return act->accepted_count; }
uint64_t actuator_failsafe_count(void) { return act->failsafe_count; }

/* Boot: put the verifier in place. The normal world has not run yet. */
void world_init(void)
{
    size_t size = (size_t)(verifier_blob_end - verifier_blob_start);

    memset((void *)TCODE_BASE, 0, TCODE_SIZE);
    memset((void *)TDATA_BASE, 0, TDATA_SIZE);          /* verifier state starts at zero */
    memset((void *)ACT_BASE, 0, sizeof(struct actuator));
    memcpy((void *)TCODE_BASE, verifier_blob_start, size);

    uart_puts("verifier: loaded, bytes = ");
    uart_puthex(size);
    uart_puts("\n");
}

/* The monitor itself drives the actuator to its safe state. */
static void failsafe(void)
{
    act->mode = MODE_LAND;
    act->failsafe_count++;
    deadman_active = 0;
}

/* M-mode dead-man's timer expired while the normal world was running. */
void deadman_expired(void)
{
    watchdog_disarm();
    if (deadman_active && act->mode == MODE_FLIGHT)
        failsafe();
}

/*
 * The shared page must be ordinary normal-world RAM. Without this check the
 * normal world could aim the monitor's copy at monitor or verifier memory.
 */
long dtee_register(uint64_t pa)
{
    if (shared_pa != 0)
        return SBI_ERR_DENIED;                          /* registered once only */
    if (pa & (PAGE_SIZE - 1))
        return SBI_ERR_INVALID_PARAM;                   /* not page aligned */
    if (pa < RAM_BASE || pa > RAM_BASE + RAM_SIZE - PAGE_SIZE)
        return SBI_ERR_INVALID_PARAM;                   /* not RAM */
    if (pa < MON_BASE + MON_SIZE)
        return SBI_ERR_INVALID_PARAM;                   /* inside the monitor */
    if (pa + PAGE_SIZE > TRUSTED_BASE)
        return SBI_ERR_INVALID_PARAM;                   /* inside the trusted world */

    shared_pa = pa;
    return SBI_SUCCESS;
}

/* One secure call. Runs with the normal world stopped, inside its ecall trap. */
static long run_trusted_req(uint32_t req_type, long *verdict)
{
    if (shared_pa == 0)
        return SBI_ERR_DENIED;

    uint64_t start_cyc   = csr_read(mcycle);
    uint64_t start_ticks = time_now_ticks();
    struct lat_stats *st = (req_type == REQ_POSE) ? &pose_stats : &mission_stats;

    /* 1. Take a private copy of the request and stamp it with trusted time.
     *    The verifier only ever sees the copy, so the normal world cannot
     *    change the command after it has been checked. */
    struct verifier_req *req = (struct verifier_req *)MAILBOX_BASE;
    req->req_type = req_type;
    if (req_type == REQ_POSE)
        memcpy(&req->cmd, (const void *)shared_pa, sizeof(req->cmd));
    else
        memcpy(&req->mission, (const void *)shared_pa, sizeof(req->mission));
    req->now_us = time_now_us();

    if (deadman_active && req->now_us >= deadman_deadline_us) {
        watchdog_disarm();
        deadman_active = 0;
        failsafe();
        *verdict = V_TIMEOUT;
        stats_record(st, start_cyc, start_ticks, csr_read(mcycle), time_now_ticks());
        return SBI_SUCCESS;
    }

    /* 2. Save the normal world's state that the switch overwrites.
     *    (Its general registers are already in the trap frame.) */
    uint64_t saved_mepc    = csr_read(mepc);
    uint64_t saved_mstatus = csr_read(mstatus);
    uint64_t saved_satp    = csr_read(satp);
    uint64_t saved_medeleg = csr_read(medeleg);
    uint64_t saved_mideleg = csr_read(mideleg);
    uint64_t saved_mie     = csr_read(mie);

    /* 3. Switch to the trusted world. */
    csr_write(medeleg, 0);                              /* every verifier trap comes to the monitor */
    csr_write(mideleg, 0);
    csr_write(mie, 0);                                  /* normal-world interrupts wait until we are back */
    csr_write(satp, 0);                                 /* no translation: VA = PA */
    pmp_set_view(PMP_VIEW_TRUSTED);
    csr_write(mstatus, (saved_mstatus & ~(MSTATUS_MPP_MASK | MSTATUS_MPIE | MSTATUS_FS_MASK | MSTATUS_VS_MASK))
                       | MSTATUS_MPP_U);                /* mret goes to U-mode, FPU/VPU off */
    uint64_t verifier_deadline = req->now_us + VERIFIER_BUDGET_US;
    if (deadman_active && deadman_deadline_us < verifier_deadline)
        verifier_deadline = deadman_deadline_us;
    watchdog_arm_deadline(verifier_deadline);
    trusted = 1;

    /* 4. Run the verifier. Comes back through trusted_trap() -> verifier_return(). */
    long v = run_verifier(TCODE_BASE, MAILBOX_BASE);

    /* 5. Switch back to the normal world. */
    trusted = 0;
    watchdog_disarm();
    csr_write(mie, saved_mie & ~MIE_MTIE);
    csr_write(mideleg, saved_mideleg);
    csr_write(medeleg, saved_medeleg);
    csr_write(satp, saved_satp);
    pmp_set_view(PMP_VIEW_NORMAL);
    csr_write(mstatus, saved_mstatus);
    csr_write(mepc, saved_mepc);

    uint64_t now_after = time_now_us();
    if (deadman_active && now_after >= deadman_deadline_us) {
        deadman_active = 0;
        watchdog_disarm();
        failsafe();
        v = V_TIMEOUT;
    }

    if (req_type == REQ_POSE) {
        if (v == V_ACCEPT && act->mode == MODE_FLIGHT) {
            deadman_active = 1;
            deadman_deadline_us = now_after + DEADMAN_TIMEOUT_US;
            watchdog_arm_deadline(deadman_deadline_us);
        } else if (act->mode != MODE_FLIGHT) {
            deadman_active = 0;
            watchdog_disarm();
        } else if (deadman_active) {
            watchdog_arm_deadline(deadman_deadline_us);
        }
    } else if (deadman_active) {
        /* Mission updates never extend the flight dead-man's deadline. */
        watchdog_arm_deadline(deadman_deadline_us);
    }

    *verdict = v;
    stats_record(st, start_cyc, start_ticks, csr_read(mcycle), time_now_ticks());
    return SBI_SUCCESS;
}

long dtee_submit(long *verdict)
{
    return run_trusted_req(REQ_POSE, verdict);
}

long dtee_mission(long *verdict)
{
    return run_trusted_req(REQ_MISSION, verdict);
}

/*
 * Every trap taken while the verifier runs ends its run.
 *   ecall            -> normal completion, a0 holds the verdict
 *   timer interrupt  -> the watchdog fired: it overran its budget
 *   anything else    -> it faulted
 * On the last two the monitor forces the safe state itself.
 */
void trusted_trap(struct trap_frame *tf, uint64_t mcause)
{
    long verdict;

    if (mcause == EXC_ECALL_U &&
        (tf->regs[REG_A0] < V_TIMEOUT || tf->regs[REG_A0] == V_REJECT_AUTH)) {
        verdict = (long)tf->regs[REG_A0];
    } else if (mcause == (MCAUSE_INTERRUPT | IRQ_M_TIMER)) {
        failsafe();
        verdict = V_TIMEOUT;
    } else {
        failsafe();
        verdict = V_FAULT;
    }

    verifier_return(verdict);
}
