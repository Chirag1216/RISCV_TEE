/*
 * Normal-world test payload: a stand-in for a compromised Linux kernel.
 * It runs in S-mode.
 *
 * Part A: tries to touch memory it must not reach. PMP has to stop every attempt:
 *         each one arrives in our own trap handler as an access fault.
 * Part B: sends commands through the secure call and checks each verdict.
 */
#include <stdint.h>
#include "dtee.h"
#include "layout.h"
#include "../verifier/policy.h"

/* Positions derived from the policy, so the test works with any calibration. */
#define CX     ((FENCE_X_MIN + FENCE_X_MAX) / 2)
#define CY     ((FENCE_Y_MIN + FENCE_Y_MAX) / 2)
#define CZ     ((FENCE_Z_MIN + FENCE_Z_MAX) / 2)
#define LIMIT  (V_MAX_MM_S * 50 / 1000 + EPS_MM)
#define CAP    (V_MAX_MM_S * DT_CAP_US / 1000000 + EPS_MM)
#define SMALL  (LIMIT / 2)
#define FAST   ((LIMIT + CAP) / 2)

/* ---- SBI call: a7 = extension, a6 = function, a0 = argument ---- */
struct sbiret {
    long error;
    long value;
};

static struct sbiret sbi_call(unsigned long eid, unsigned long fid, unsigned long arg0)
{
    register unsigned long a0 asm("a0") = arg0;
    register unsigned long a1 asm("a1") = 0;
    register unsigned long a6 asm("a6") = fid;
    register unsigned long a7 asm("a7") = eid;
    asm volatile ("ecall" : "+r"(a0), "+r"(a1) : "r"(a6), "r"(a7) : "memory");
    return (struct sbiret){ (long)a0, (long)a1 };
}

static void puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            sbi_call(SBI_EID_PUTCHAR, 0, '\r');
        sbi_call(SBI_EID_PUTCHAR, 0, (unsigned long)*s++);
    }
}

static long debug(unsigned long fid)
{
    return sbi_call(SBI_EID_DEBUG, fid, 0).value;
}

/* Busy-wait using the time counter (10 ticks per microsecond on QEMU virt). */
static void delay_ms(unsigned long ms)
{
    unsigned long start, now;
    asm volatile ("rdtime %0" : "=r"(start));
    do {
        asm volatile ("rdtime %0" : "=r"(now));
    } while (now - start < ms * (TIMEBASE_HZ / 1000));
}

static int failures;

static void result(int ok, const char *what)
{
    puts(ok ? "  PASS  " : "  FAIL  ");
    puts(what);
    puts("\n");
    if (!ok)
        failures++;
}

/* ------------------------------------------------------------------ */
/* Part A: isolation                                                   */
/* ------------------------------------------------------------------ */

static volatile uint64_t own_memory;
volatile uint64_t fault_count;      /* incremented by the trap handler in start.S */

static void isolation_tests(void)
{
    volatile uint64_t *p;
    volatile uint64_t sink;
    long before;

    puts("--- Part A: isolation ---\n");

    before = (long)fault_count;
    p = (volatile uint64_t *)MON_BASE;
    sink = *p;
    result((long)fault_count == before + 1, "monitor code is not readable");

    before = (long)fault_count;
    p = (volatile uint64_t *)(MON_BASE + 0x40000);
    *p = 0xdeadbeef;
    result((long)fault_count == before + 1, "monitor data is not writable");

    before = (long)fault_count;
    p = (volatile uint64_t *)TCODE_BASE;
    *p = 0;
    result((long)fault_count == before + 1, "verifier code is not writable");

    before = (long)fault_count;
    p = (volatile uint64_t *)TDATA_BASE;
    sink = *p;
    result((long)fault_count == before + 1, "verifier data is not readable");

    before = (long)fault_count;
    p = (volatile uint64_t *)ACT_BASE;
    *p = 0x1234;
    result((long)fault_count == before + 1, "actuator is not writable from the normal world");

    before = (long)fault_count;
    own_memory = 42;
    result((long)fault_count == before, "normal-world memory still works");

    (void)sink;
}

/* ------------------------------------------------------------------ */
/* Part B: the secure call                                             */
/* ------------------------------------------------------------------ */

/* The page shared with the monitor. Page-aligned, ordinary normal-world RAM. */
static struct pose_cmd shared_page[PAGE_SIZE / sizeof(struct pose_cmd)]
    __attribute__((aligned(PAGE_SIZE)));

static const char *const verdict_names[V_COUNT] = {
    "ACCEPT", "REJECT_SEQ", "REJECT_RATE", "REJECT_MODE", "REJECT_FENCE",
    "REJECT_JUMP", "REJECT_SPEED", "TIMEOUT", "FAULT",
};

/* Send one command and check that the verdict is the expected one. */
static void send(const char *what, long expect,
                 uint64_t seq, int32_t x, int32_t y, int32_t z, uint32_t mode)
{
    struct pose_cmd *c = &shared_page[0];
    c->seq = seq;
    c->t_us = 0;
    c->x_mm = x;
    c->y_mm = y;
    c->z_mm = z;
    c->yaw_mdeg = 0;
    c->mode = mode;

    struct sbiret r = sbi_call(SBI_EID_DTEE, DTEE_FID_SUBMIT, 0);
    int ok = (r.error == SBI_SUCCESS && r.value == expect);

    puts(ok ? "  PASS  " : "  FAIL  ");
    puts(what);
    puts(" -> ");
    puts(r.error != SBI_SUCCESS ? "SBI ERROR"
         : (r.value >= 0 && r.value < V_COUNT) ? verdict_names[r.value] : "?");
    puts("\n");
    if (!ok)
        failures++;
}

static void secure_call_tests(void)
{
    struct sbiret r;

    puts("\n--- Part B: secure call ---\n");

    /* Shared page registration */
    r = sbi_call(SBI_EID_DTEE, DTEE_FID_SUBMIT, 0);
    result(r.error == SBI_ERR_DENIED, "submit before registering is refused");

    r = sbi_call(SBI_EID_DTEE, DTEE_FID_REGISTER, MON_BASE + 0x40000);
    result(r.error == SBI_ERR_INVALID_PARAM, "monitor memory refused as shared page");

    r = sbi_call(SBI_EID_DTEE, DTEE_FID_REGISTER, TDATA_BASE);
    result(r.error == SBI_ERR_INVALID_PARAM, "verifier memory refused as shared page");

    r = sbi_call(SBI_EID_DTEE, DTEE_FID_REGISTER, (unsigned long)shared_page);
    result(r.error == SBI_SUCCESS, "normal-world page accepted as shared page");

    /* Good commands. 50 ms apart = 20 Hz. Each step is half the allowed limit. */
    send("arm at the centre              ", V_ACCEPT,       1, CX,             CY, CZ, MODE_ARMED);
    delay_ms(50);
    send("take off, small step           ", V_ACCEPT,       2, CX + SMALL,     CY, CZ, MODE_FLIGHT);
    delay_ms(50);
    send("fly, small step                ", V_ACCEPT,       3, CX + 2 * SMALL, CY, CZ, MODE_FLIGHT);
    result(debug(DEBUG_FID_ACCEPTED) == 3, "actuator received exactly 3 commands");

    /* Attacks. Each one is 50 ms after the last accepted command unless noted. */
    delay_ms(50);
    send("replay an old sequence number  ", V_REJECT_SEQ,   2, CX + 3 * SMALL,     CY, CZ, MODE_FLIGHT);
    send("geofence breach                ", V_REJECT_FENCE, 4, FENCE_X_MAX + 1000, CY, CZ, MODE_FLIGHT);
    send("teleport                       ", V_REJECT_JUMP,  4, FENCE_X_MAX - 100,  CY, CZ, MODE_FLIGHT);
    send("overspeed                      ", V_REJECT_SPEED, 4, CX + 2 * SMALL + FAST, CY, CZ, MODE_FLIGHT);
    send("illegal mode change to DISARMED", V_REJECT_MODE,  4, CX + 3 * SMALL,     CY, CZ, MODE_DISARMED);
    send("unknown mode                   ", V_REJECT_MODE,  4, CX + 3 * SMALL,     CY, CZ, 77);
    send("valid command                  ", V_ACCEPT,       4, CX + 3 * SMALL,     CY, CZ, MODE_FLIGHT);
    send("flood (no delay after accept)  ", V_REJECT_RATE,  5, CX + 3 * SMALL + 10, CY, CZ, MODE_FLIGHT);
    result(debug(DEBUG_FID_ACCEPTED) == 4, "rejected commands never reached the actuator");

    /* The verifier itself misbehaves (test hooks). */
    delay_ms(50);
    send("verifier hangs                 ", V_TIMEOUT,      6, CX + 3 * SMALL, CY, CZ, 0xDEAD);
    result(debug(DEBUG_FID_FAILSAFE) == 1, "watchdog forced the safe state");
    send("verifier touches normal memory ", V_FAULT,        6, CX + 3 * SMALL, CY, CZ, 0xBAD);
    result(debug(DEBUG_FID_FAILSAFE) == 2, "fault forced the safe state");

    /* Post-failsafe state lockout: immediate FLIGHT must be rejected. */
    send("flight blocked after failsafe  ", V_REJECT_MODE,  6, CX + 3 * SMALL, CY, CZ, MODE_FLIGHT);

    /* Clean recovery: DISARMED -> ARMED -> FLIGHT. */
    delay_ms(50);
    send("recover: disarm after failsafe ", V_ACCEPT,       6, CX + 3 * SMALL, CY, CZ, MODE_DISARMED);
    delay_ms(50);
    send("recover: arm                   ", V_ACCEPT,       7, CX + 3 * SMALL, CY, CZ, MODE_ARMED);
    delay_ms(50);
    send("recover: take off              ", V_ACCEPT,       8, CX + 3 * SMALL, CY, CZ, MODE_FLIGHT);

    /* Dead-man's switch: sleeping 600 ms in FLIGHT without commands triggers failsafe. */
    delay_ms(600);
    result(debug(DEBUG_FID_FAILSAFE) == 3, "deadman forced safe state after 600 ms");
    send("flight blocked after deadman   ", V_REJECT_MODE,  9, CX + 3 * SMALL, CY, CZ, MODE_FLIGHT);
    send("land after deadman             ", V_ACCEPT,       9, CX + 3 * SMALL, CY, CZ, MODE_LAND);
}

void payload_main(void)
{
    puts("payload: running in S-mode\n\n");

    isolation_tests();
    secure_call_tests();

    puts(failures == 0 ? "\npayload: ALL TESTS PASSED\n"
                       : "\npayload: SOME TESTS FAILED\n");
}
