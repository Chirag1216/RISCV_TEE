/*
 * dtee_test: checks the secure call from a Linux user program.
 *
 *   user program --ioctl--> /dev/dtee driver --ecall--> monitor --> verifier
 *
 * Sends good commands and attacks, and compares each verdict with the expected one.
 * It ends by landing and disarming, so it can be run again in the same boot.
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "dtee_ioctl.h"
#include "policy.h"         /* the verifier's limits: the test sizes its steps from them */

/* Positions derived from the policy, so the test works with any calibration. */
#define CX   ((FENCE_X_MIN + FENCE_X_MAX) / 2)          /* centre of the geofence */
#define CY   ((FENCE_Y_MIN + FENCE_Y_MAX) / 2)
#define CZ   ((FENCE_Z_MIN + FENCE_Z_MAX) / 2)
#define LIMIT  (V_MAX_MM_S * 50 / 1000 + EPS_MM)         /* largest step allowed in 50 ms */
#define CAP    (V_MAX_MM_S * DT_CAP_US / 1000000 + EPS_MM) /* largest step ever allowed  */
#define SMALL  (LIMIT / 2)                               /* always accepted               */
#define FAST   ((LIMIT + CAP) / 2)                       /* too fast, but not a teleport  */

static const char *const verdict_names[V_COUNT] = {
    "ACCEPT", "REJECT_SEQ", "REJECT_RATE", "REJECT_MODE", "REJECT_FENCE",
    "REJECT_JUMP", "REJECT_SPEED", "TIMEOUT", "FAULT",
};

static int fd;
static int failures;
static uint64_t seq_base;       /* makes sequence numbers increase across runs */

static void result(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        failures++;
}

static void send(const char *what, uint32_t expect,
                 uint64_t seq, int32_t x, int32_t y, int32_t z, uint32_t mode)
{
    struct dtee_submit s = { 0 };

    s.cmd.seq = seq_base + seq;
    s.cmd.x_mm = x;
    s.cmd.y_mm = y;
    s.cmd.z_mm = z;
    s.cmd.mode = mode;

    if (ioctl(fd, DTEE_IOC_SUBMIT, &s) != 0) {
        printf("  FAIL  %s -> ioctl error\n", what);
        failures++;
        return;
    }

    int ok = (s.verdict == expect);
    printf("  %s  %s -> %s\n", ok ? "PASS" : "FAIL", what,
           s.verdict < V_COUNT ? verdict_names[s.verdict] : "?");
    if (!ok)
        failures++;
}

static struct dtee_stats stats(void)
{
    struct dtee_stats st = { 0 };
    ioctl(fd, DTEE_IOC_STATS, &st);
    return st;
}

static void wait_50ms(void)
{
    usleep(50000);
}

int main(void)
{
    struct timespec now;
    struct dtee_stats before, after;
    int blocked = 0;

    fd = open("/dev/dtee", O_RDWR);
    if (fd < 0) {
        perror("open /dev/dtee");
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &now);
    seq_base = (uint64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;

    printf("--- isolation, from inside the Linux kernel ---\n");
    ioctl(fd, DTEE_IOC_TEST_BYPASS, &blocked);
    result(blocked == 1, "kernel write to the actuator is blocked by PMP");

    printf("\n--- secure call, from a Linux user program ---\n");
    before = stats();

    /* A previous run may have left the setpoint away from the centre. Wait long
     * enough that moving back is judged against the teleport cap, not the speed limit. */
    usleep(300000);

    /* Good commands, 50 ms apart (20 Hz). Each step is half the allowed limit. */
    send("arm at the centre              ", V_ACCEPT,       1, CX,             CY, CZ, MODE_ARMED);
    wait_50ms();
    send("take off, small step           ", V_ACCEPT,       2, CX + SMALL,     CY, CZ, MODE_FLIGHT);
    wait_50ms();
    send("fly, small step                ", V_ACCEPT,       3, CX + 2 * SMALL, CY, CZ, MODE_FLIGHT);

    /* Attacks */
    wait_50ms();
    send("replay an old sequence number  ", V_REJECT_SEQ,   2, CX + 3 * SMALL,     CY, CZ, MODE_FLIGHT);
    send("geofence breach                ", V_REJECT_FENCE, 4, FENCE_X_MAX + 1000, CY, CZ, MODE_FLIGHT);
    send("teleport                       ", V_REJECT_JUMP,  4, FENCE_X_MAX - 100,  CY, CZ, MODE_FLIGHT);
    send("overspeed                      ", V_REJECT_SPEED, 4, CX + 2 * SMALL + FAST, CY, CZ, MODE_FLIGHT);
    send("illegal mode change to DISARMED", V_REJECT_MODE,  4, CX + 3 * SMALL,     CY, CZ, MODE_DISARMED);
    send("valid command                  ", V_ACCEPT,       4, CX + 3 * SMALL,     CY, CZ, MODE_FLIGHT);
    send("flood (no delay after accept)  ", V_REJECT_RATE,  5, CX + 3 * SMALL + 10, CY, CZ, MODE_FLIGHT);

    after = stats();
    result(after.accepted - before.accepted == 4, "only the 4 valid commands reached the actuator");

    /* The verifier itself misbehaves (test hooks in the verifier build). */
    wait_50ms();
    send("verifier hangs                 ", V_TIMEOUT,      6, CX + 3 * SMALL, CY, CZ, 0xDEAD);
    send("verifier touches normal memory ", V_FAULT,        6, CX + 3 * SMALL, CY, CZ, 0xBAD);
    result(stats().failsafe - before.failsafe == 2, "the monitor forced the safe state twice");

    /* Finish cleanly: land and disarm. */
    send("land                           ", V_ACCEPT,       6, CX + 3 * SMALL, CY, CZ, MODE_LAND);
    wait_50ms();
    send("disarm                         ", V_ACCEPT,       7, CX + 3 * SMALL, CY, CZ, MODE_DISARMED);

    printf("\n%s\n", failures == 0 ? "dtee_test: ALL TESTS PASSED" : "dtee_test: SOME TESTS FAILED");
    close(fd);
    return failures != 0;
}
