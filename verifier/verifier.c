/*
 * The trusted verifier. Runs in U-mode, in the trusted world.
 * It is the only code that can write the actuator.
 *
 * Input : one request in the mailbox, placed there by the monitor.
 * Output: a verdict. The actuator is written only on V_ACCEPT.
 *
 * Rules: integers only (no FPU), no library, no system services.
 */
#include <stdint.h>
#include "dtee.h"
#include "layout.h"
#include "policy.h"

/* State kept between calls. It lives in trusted data, out of the normal world's reach. */
static struct {
    int      have_last;     /* 0 until the first command is accepted */
    uint64_t seq;
    uint64_t t_us;          /* trusted time of the last accepted command */
    int32_t  x_mm, y_mm, z_mm;
    uint32_t mode;          /* starts as MODE_DISARMED (0) */
} last;

/* allowed[from][to] = 1 if the mode change is permitted. */
static const uint8_t allowed[MODE_COUNT][MODE_COUNT] = {
    /* to:          DISARMED ARMED FLIGHT LAND */
    /* DISARMED */ { 1,       1,    0,     0 },
    /* ARMED    */ { 1,       1,    1,     0 },
    /* FLIGHT   */ { 0,       0,    1,     1 },
    /* LAND     */ { 1,       0,    0,     1 },
};

static int check(const struct verifier_req *req)
{
    const struct pose_cmd *c = &req->cmd;

    /* 1. Replay: the sequence number must strictly increase. */
    if (last.have_last && c->seq <= last.seq)
        return V_REJECT_SEQ;

    /* 2. Rate: not sooner than MIN_PERIOD_US after the last accepted command. */
    uint64_t dt_us = req->now_us - last.t_us;
    if (last.have_last && dt_us < MIN_PERIOD_US)
        return V_REJECT_RATE;

    /* 3. Mode: must be a known mode and a permitted transition. */
    if (c->mode >= MODE_COUNT || !allowed[last.mode][c->mode])
        return V_REJECT_MODE;

    /* 4. Geofence: the setpoint must be inside the box. */
    if (c->x_mm < FENCE_X_MIN || c->x_mm > FENCE_X_MAX ||
        c->y_mm < FENCE_Y_MIN || c->y_mm > FENCE_Y_MAX ||
        c->z_mm < FENCE_Z_MIN || c->z_mm > FENCE_Z_MAX)
        return V_REJECT_FENCE;

    /* 5 and 6. Step size, compared as squared distances so no sqrt is needed. */
    if (last.have_last) {
        int64_t dx = (int64_t)c->x_mm - last.x_mm;
        int64_t dy = (int64_t)c->y_mm - last.y_mm;
        int64_t dz = (int64_t)c->z_mm - last.z_mm;
        int64_t dist2 = dx * dx + dy * dy + dz * dz;

        /* 5. Teleport: larger than the biggest step ever allowed. */
        int64_t cap = (int64_t)V_MAX_MM_S * DT_CAP_US / 1000000 + EPS_MM;
        if (dist2 > cap * cap)
            return V_REJECT_JUMP;

        /* 6. Speed: larger than v_max * dt + eps for the time that really passed. */
        if (dt_us < DT_CAP_US) {
            int64_t lim = (int64_t)V_MAX_MM_S * (int64_t)dt_us / 1000000 + EPS_MM;
            if (dist2 > lim * lim)
                return V_REJECT_SPEED;
        }
    }

    return V_ACCEPT;
}

#ifdef VERIFIER_TEST_HOOKS
/* Test only: let the monitor's watchdog and fault handling be exercised. */
#define TEST_MODE_HANG   0xDEAD     /* loop forever                */
#define TEST_MODE_FAULT  0xBAD      /* touch normal-world memory   */
#endif

long verify(const struct verifier_req *req)
{
#ifdef VERIFIER_TEST_HOOKS
    if (req->cmd.mode == TEST_MODE_HANG)
        for (;;)
            ;
    if (req->cmd.mode == TEST_MODE_FAULT)
        return *(volatile long *)NORMAL_BASE;
#endif

    int verdict = check(req);
    if (verdict != V_ACCEPT)
        return verdict;

    /* Accepted: remember it and drive the actuator. */
    const struct pose_cmd *c = &req->cmd;
    last.have_last = 1;
    last.seq  = c->seq;
    last.t_us = req->now_us;
    last.x_mm = c->x_mm;
    last.y_mm = c->y_mm;
    last.z_mm = c->z_mm;
    last.mode = c->mode;

    volatile struct actuator *act = (volatile struct actuator *)ACT_BASE;
    act->seq      = c->seq;
    act->x_mm     = c->x_mm;
    act->y_mm     = c->y_mm;
    act->z_mm     = c->z_mm;
    act->yaw_mdeg = c->yaw_mdeg;
    act->mode     = c->mode;
    act->accepted_count++;

    return V_ACCEPT;
}
