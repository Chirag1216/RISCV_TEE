/*
 * Normal-world test payload: a stand-in for a compromised Linux kernel.
 * It runs in S-mode.
 *
 * Part A: tries to touch memory it must not reach. PMP has to stop every attempt:
 *         each one arrives in our own trap handler as an access fault.
 * Part B: sends commands through the secure call and checks each verdict.
 */
#include <stddef.h>
#include <stdint.h>
#include "dtee.h"
#include "layout.h"
#include "../verifier/hmac_sha256.h"
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

static void putdec(uint64_t v)
{
    char buf[21];
    int i = 0;

    if (v == 0) {
        sbi_call(SBI_EID_PUTCHAR, 0, '0');
        return;
    }
    while (v > 0) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0)
        sbi_call(SBI_EID_PUTCHAR, 0, (unsigned long)buf[--i]);
}

static inline uint64_t rdcycle(void)
{
    uint64_t v;
    asm volatile ("rdcycle %0" : "=r"(v));
    return v;
}

static inline uint64_t rdtime(void)
{
    uint64_t v;
    asm volatile ("rdtime %0" : "=r"(v));
    return v;
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
    "REJECT_JUMP", "REJECT_SPEED", "TIMEOUT", "FAULT", "REJECT_AUTH",
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

static void send_mission(const char *what, long expect, const struct gcs_mission *m)
{
    volatile struct gcs_mission *dst = (volatile struct gcs_mission *)shared_page;
    const volatile uint8_t *s = (const volatile uint8_t *)m;
    volatile uint8_t *d = (volatile uint8_t *)dst;
    for (unsigned long i = 0; i < sizeof(*m); i++)
        d[i] = s[i];

    struct sbiret r = sbi_call(SBI_EID_DTEE, DTEE_FID_MISSION, 0);
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

static void mission_corridor_tests(void)
{
    puts("\n--- Part C: signed GCS mission corridor ---\n");

    /* Simulates the off-board GCS test harness; in linux/prebuilt/ and real deployments, Linux never possesses K_GCS. */
    static const uint8_t k_gcs[32] = {
        0x52, 0x56, 0x54, 0x45, 0x45, 0x5f, 0x47, 0x43,
        0x53, 0x5f, 0x4b, 0x45, 0x59, 0x5f, 0x30, 0x31,
        0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01,
        0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
    };

    struct gcs_mission m;
    volatile uint8_t *mp = (volatile uint8_t *)&m;
    for (unsigned long i = 0; i < sizeof(m); i++)
        mp[i] = 0;

    m.mission_seq = 1;
    m.fence_x_min = FENCE_X_MIN;
    m.fence_x_max = FENCE_X_MAX;
    m.fence_y_min = FENCE_Y_MIN;
    m.fence_y_max = FENCE_Y_MAX;
    m.fence_z_min = FENCE_Z_MIN;
    m.fence_z_max = FENCE_Z_MAX;
    m.v_max_mm_s = 0;
    m.corridor_radius_mm = 300;
    m.wp_count = 2;
    m.wps[0].x_mm = CX;        m.wps[0].y_mm = CY; m.wps[0].z_mm = CZ;
    m.wps[1].x_mm = CX + 1000; m.wps[1].y_mm = CY; m.wps[1].z_mm = CZ;

    uint8_t valid_hmac[32];
    hmac_sha256(k_gcs, sizeof(k_gcs),
                (const uint8_t *)&m, offsetof(struct gcs_mission, hmac),
                valid_hmac);

    /* 1. Forged mission (zeroed HMAC). */
    send_mission("forged mission (zero HMAC)     ", V_REJECT_AUTH, &m);

    /* 2. Tampered mission (valid HMAC, 1 bit flipped in fence_x_max). */
    for (int i = 0; i < 32; i++)
        m.hmac[i] = valid_hmac[i];
    m.fence_x_max ^= 1;
    send_mission("tampered mission (1 bit flip)  ", V_REJECT_AUTH, &m);
    m.fence_x_max ^= 1;

    /* 3. Valid signed mission (R = 300 mm corridor). */
    send_mission("valid signed mission           ", V_ACCEPT, &m);

    /* 4. Replayed mission (same mission_seq). */
    send_mission("replayed mission_seq           ", V_REJECT_SEQ, &m);

    /* 5. Enter FLIGHT on the corridor, then drift +10 mm/step in +Y. */
    delay_ms(50);
    send("mission: disarm                ", V_ACCEPT, 10, CX + 3 * SMALL, CY, CZ, MODE_DISARMED);
    delay_ms(50);
    send("mission: arm                   ", V_ACCEPT, 11, CX + 3 * SMALL, CY, CZ, MODE_ARMED);
    delay_ms(50);
    send("mission: take off              ", V_ACCEPT, 12, CX + 3 * SMALL, CY, CZ, MODE_FLIGHT);

    uint64_t seq = 13;
    int drift_ok = 1;
    for (int dy = 10; dy <= 300; dy += 10) {
        delay_ms(50);
        struct pose_cmd *c = &shared_page[0];
        c->seq = seq++;
        c->t_us = 0;
        c->x_mm = CX + 3 * SMALL;
        c->y_mm = CY + dy;
        c->z_mm = CZ;
        c->yaw_mdeg = 0;
        c->mode = MODE_FLIGHT;
        struct sbiret r = sbi_call(SBI_EID_DTEE, DTEE_FID_SUBMIT, 0);
        if (r.error != SBI_SUCCESS || r.value != V_ACCEPT) {
            drift_ok = 0;
            break;
        }
    }
    result(drift_ok, "slow +Y drift accepted inside y <= 300 mm");

    delay_ms(50);
    send("drift y=310 outside corridor   ", V_REJECT_FENCE, seq++, CX + 3 * SMALL, CY + 310, CZ, MODE_FLIGHT);
    send("mission: land                  ", V_ACCEPT,       seq++, CX + 3 * SMALL, CY + 300, CZ, MODE_LAND);
}

struct bench_stat {
    uint64_t count;
    uint64_t min_cyc, max_cyc, sum_cyc;
    uint64_t min_ticks, max_ticks, sum_ticks;
};

static void bench_record(struct bench_stat *st,
                         uint64_t c0, uint64_t t0, uint64_t c1, uint64_t t1)
{
    uint64_t dc = c1 - c0;
    uint64_t dt = t1 - t0;
    if (st->count == 0 || dc < st->min_cyc) st->min_cyc = dc;
    if (dc > st->max_cyc) st->max_cyc = dc;
    st->sum_cyc += dc;
    if (st->count == 0 || dt < st->min_ticks) st->min_ticks = dt;
    if (dt > st->max_ticks) st->max_ticks = dt;
    st->sum_ticks += dt;
    st->count++;
}

static void bench_print(const char *name, const struct bench_stat *st)
{
    puts("  ");
    puts(name);
    puts(": n=");
    putdec(st->count);
    puts("  cyc min/avg/max=");
    putdec(st->min_cyc);
    puts("/");
    putdec(st->count ? st->sum_cyc / st->count : 0);
    puts("/");
    putdec(st->max_cyc);
    puts("  ticks(0.1us) min/avg/max=");
    putdec(st->min_ticks);
    puts("/");
    putdec(st->count ? st->sum_ticks / st->count : 0);
    puts("/");
    putdec(st->max_ticks);
    puts("\n");
}

static void protc_benchmark_tests(void)
{
    puts("\n--- Part D: PROTC micro-benchmarks ---\n");

    /* 1. DI-ACCESS: 50 direct volatile writes in normal-world RAM */
    volatile struct actuator dummy_act = { 0 };
    struct bench_stat di_st = { 0 };
    for (int i = 0; i < 50; i++) {
        uint64_t c0 = rdcycle(), t0 = rdtime();
        dummy_act.seq = (uint64_t)(i + 1);
        dummy_act.x_mm = 100;
        dummy_act.y_mm = 0;
        dummy_act.z_mm = 1000;
        dummy_act.yaw_mdeg = 0;
        dummy_act.mode = MODE_FLIGHT;
        dummy_act.accepted_count++;
        uint64_t c1 = rdcycle(), t1 = rdtime();
        bench_record(&di_st, c0, t0, c1, t1);
    }
    result(dummy_act.accepted_count == 50, "DI-ACCESS: 50/50 direct writes completed");

    /* 2. AUTH-FULL: 50 valid DTEE_FID_SUBMIT poses + 10 DTEE_FID_MISSION calls */
    delay_ms(200);
    uint64_t seq = 100;
    send("bench: disarm                  ", V_ACCEPT, seq++, CX + SMALL, CY, CZ, MODE_DISARMED);
    delay_ms(50);
    send("bench: arm                     ", V_ACCEPT, seq++, CX + SMALL, CY, CZ, MODE_ARMED);
    delay_ms(50);
    send("bench: take off                ", V_ACCEPT, seq++, CX + SMALL, CY, CZ, MODE_FLIGHT);

    struct bench_stat auth_pose_st = { 0 };
    int auth_pose_ok = 0;
    for (int i = 0; i < 50; i++) {
        delay_ms(25);
        struct pose_cmd *c = &shared_page[0];
        c->seq = seq++;
        c->t_us = 0;
        c->x_mm = CX + SMALL + ((i & 1) ? 20 : 0);
        c->y_mm = CY;
        c->z_mm = CZ;
        c->yaw_mdeg = 0;
        c->mode = MODE_FLIGHT;
        uint64_t c0 = rdcycle(), t0 = rdtime();
        struct sbiret r = sbi_call(SBI_EID_DTEE, DTEE_FID_SUBMIT, 0);
        uint64_t c1 = rdcycle(), t1 = rdtime();
        bench_record(&auth_pose_st, c0, t0, c1, t1);
        if (r.error == SBI_SUCCESS && r.value == V_ACCEPT)
            auth_pose_ok++;
    }
    result(auth_pose_ok == 50, "AUTH-FULL: 50/50 pose commands accepted");

    struct bench_stat auth_mis_st = { 0 };
    int auth_mis_ok = 0;
    {
        volatile struct gcs_mission *dst = (volatile struct gcs_mission *)shared_page;
        volatile uint8_t *dp = (volatile uint8_t *)dst;
        for (unsigned long i = 0; i < sizeof(*dst); i++)
            dp[i] = 0;
        dst->mission_seq = 99;
        dst->fence_x_min = FENCE_X_MIN; dst->fence_x_max = FENCE_X_MAX;
        dst->fence_y_min = FENCE_Y_MIN; dst->fence_y_max = FENCE_Y_MAX;
        dst->fence_z_min = FENCE_Z_MIN; dst->fence_z_max = FENCE_Z_MAX;
        dst->wp_count = 2;
        for (int i = 0; i < 10; i++) {
            uint64_t c0 = rdcycle(), t0 = rdtime();
            struct sbiret r = sbi_call(SBI_EID_DTEE, DTEE_FID_MISSION, 0);
            uint64_t c1 = rdcycle(), t1 = rdtime();
            bench_record(&auth_mis_st, c0, t0, c1, t1);
            if (r.error == SBI_SUCCESS && r.value == V_REJECT_AUTH)
                auth_mis_ok++;
        }
    }
    result(auth_mis_ok == 10, "AUTH-FULL: 10/10 mission HMAC checks timed");

    /* 3. AUTH-PART: 20 mixed commands (10 valid + 10 policy violations) */
    long before_acc = debug(DEBUG_FID_ACCEPTED);
    int part_valid_ok = 0;
    int part_rej_ok = 0;
    for (int i = 0; i < 10; i++) {
        delay_ms(25);
        struct pose_cmd *c = &shared_page[0];
        c->seq = seq++;
        c->t_us = 0;
        c->x_mm = CX + SMALL + ((i & 1) ? 20 : 0);
        c->y_mm = CY;
        c->z_mm = CZ;
        c->yaw_mdeg = 0;
        c->mode = MODE_FLIGHT;
        struct sbiret r = sbi_call(SBI_EID_DTEE, DTEE_FID_SUBMIT, 0);
        if (r.error == SBI_SUCCESS && r.value == V_ACCEPT)
            part_valid_ok++;

        delay_ms(25);
        int kind = i % 4;
        if (kind == 0) {
            c->seq = seq++;
            c->x_mm = CX + SMALL; c->y_mm = CY + 400; c->z_mm = CZ; c->mode = MODE_FLIGHT;
            r = sbi_call(SBI_EID_DTEE, DTEE_FID_SUBMIT, 0);
            if (r.error == SBI_SUCCESS && r.value == V_REJECT_FENCE) part_rej_ok++;
        } else if (kind == 1) {
            c->seq = seq++;
            c->x_mm = CX + SMALL + FAST; c->y_mm = CY; c->z_mm = CZ; c->mode = MODE_FLIGHT;
            r = sbi_call(SBI_EID_DTEE, DTEE_FID_SUBMIT, 0);
            if (r.error == SBI_SUCCESS && r.value == V_REJECT_SPEED) part_rej_ok++;
        } else if (kind == 2) {
            c->seq = seq++;
            c->x_mm = CX + SMALL; c->y_mm = CY; c->z_mm = CZ; c->mode = MODE_DISARMED;
            r = sbi_call(SBI_EID_DTEE, DTEE_FID_SUBMIT, 0);
            if (r.error == SBI_SUCCESS && r.value == V_REJECT_MODE) part_rej_ok++;
        } else {
            volatile struct gcs_mission *dst = (volatile struct gcs_mission *)shared_page;
            volatile uint8_t *dp = (volatile uint8_t *)dst;
            for (unsigned long k = 0; k < sizeof(*dst); k++)
                dp[k] = 0;
            dst->mission_seq = 99;
            dst->fence_x_min = FENCE_X_MIN; dst->fence_x_max = FENCE_X_MAX;
            dst->fence_y_min = FENCE_Y_MIN; dst->fence_y_max = FENCE_Y_MAX;
            dst->fence_z_min = FENCE_Z_MIN; dst->fence_z_max = FENCE_Z_MAX;
            r = sbi_call(SBI_EID_DTEE, DTEE_FID_MISSION, 0);
            if (r.error == SBI_SUCCESS && r.value == V_REJECT_AUTH) part_rej_ok++;
        }
    }
    result(part_valid_ok == 10 && part_rej_ok == 10 &&
           debug(DEBUG_FID_ACCEPTED) == before_acc + 10,
           "AUTH-PART: 10/10 valid accepted, 10/10 violations blocked");

    /* 4. UNAUTH: 8 direct S-mode memory attacks across MON, TCODE, TDATA, ACT */
    long acc_before_atk = debug(DEBUG_FID_ACCEPTED);
    long f0 = (long)fault_count;
    volatile uint64_t sink = 0;
    sink += *(volatile uint64_t *)MON_BASE;
    *(volatile uint64_t *)(MON_BASE + 0x40000) = 0xdeadbeef;
    sink += *(volatile uint64_t *)TCODE_BASE;
    *(volatile uint64_t *)TCODE_BASE = 0;
    sink += *(volatile uint64_t *)TDATA_BASE;
    *(volatile uint64_t *)TDATA_BASE = 0;
    sink += *(volatile uint64_t *)ACT_BASE;
    *(volatile uint64_t *)ACT_BASE = 0x1234;
    (void)sink;
    result((long)fault_count == f0 + 8 && debug(DEBUG_FID_ACCEPTED) == acc_before_atk,
           "UNAUTH: 8/8 direct memory attacks trapped by PMP");

    /* Land cleanly at the end of Part D. */
    delay_ms(25);
    send("bench: land                    ", V_ACCEPT, seq++, CX + SMALL, CY, CZ, MODE_LAND);

    puts("\nPROTC micro-benchmark summary (S-mode):\n");
    bench_print("DI-ACCESS       ", &di_st);
    bench_print("AUTH-FULL(pose) ", &auth_pose_st);
    bench_print("AUTH-FULL(hmac) ", &auth_mis_st);
}

void payload_main(void)
{
    puts("payload: running in S-mode\n\n");

    isolation_tests();
    secure_call_tests();
    mission_corridor_tests();
    protc_benchmark_tests();

    sbi_call(SBI_EID_DEBUG, DEBUG_FID_PRINT_STATS, 0);

    puts(failures == 0 ? "\npayload: ALL TESTS PASSED\n"
                       : "\npayload: SOME TESTS FAILED\n");
}
