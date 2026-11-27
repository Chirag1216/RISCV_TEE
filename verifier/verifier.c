/*
 * The trusted verifier. Runs in U-mode, in the trusted world.
 * It is the only code that can write the actuator.
 *
 * Input : one request in the mailbox, placed there by the monitor.
 * Output: a verdict. The actuator is written only on V_ACCEPT.
 *
 * Rules: integers only (no FPU), no library, no system services.
 */
#include <stddef.h>
#include <stdint.h>
#include "dtee.h"
#include "hmac_sha256.h"
#include "layout.h"
#include "policy.h"

/* Pre-shared GCS authentication key (lives in .rodata inside PMP-locked TCODE). */
static const uint8_t K_GCS[32] = {
    0x52, 0x56, 0x54, 0x45, 0x45, 0x5f, 0x47, 0x43,
    0x53, 0x5f, 0x4b, 0x45, 0x59, 0x5f, 0x30, 0x31,
    0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01,
    0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
};

/* Active signed GCS mission (zero-initialized in .bss). */
static struct {
    int      active;
    uint64_t mission_seq;
    int32_t  fence_x_min, fence_x_max;
    int32_t  fence_y_min, fence_y_max;
    int32_t  fence_z_min, fence_z_max;
    uint32_t v_max_mm_s;
    uint32_t corridor_radius_mm;
    uint32_t wp_count;
    struct waypoint wps[MAX_WAYPOINTS];
} mission;

/* State kept between calls. It lives in trusted data, out of the normal world's reach. */
static struct {
    int      have_last;     /* 0 until the first command is accepted */
    uint64_t seq;
    uint64_t t_us;          /* trusted time of the last accepted command */
    int32_t  x_mm, y_mm, z_mm;
    uint32_t mode;          /* starts as MODE_DISARMED (0) */
    uint64_t failsafe_count;
} last;

/* allowed[from][to] = 1 if the mode change is permitted. */
static const uint8_t allowed[MODE_COUNT][MODE_COUNT] = {
    /* to:          DISARMED ARMED FLIGHT LAND */
    /* DISARMED */ { 1,       1,    0,     0 },
    /* ARMED    */ { 1,       1,    1,     0 },
    /* FLIGHT   */ { 0,       0,    1,     1 },
    /* LAND     */ { 1,       0,    0,     1 },
};

/*
 * Check if point (px, py, pz) is within mission.corridor_radius_mm of at least
 * one segment wps[i] -> wps[i+1], using pure integer dot-product math.
 */
static int in_corridor(int32_t px, int32_t py, int32_t pz)
{
    __int128 r2 = (__int128)mission.corridor_radius_mm * mission.corridor_radius_mm;

    for (uint32_t i = 0; i + 1 < mission.wp_count; i++) {
        int64_t ax = mission.wps[i].x_mm;
        int64_t ay = mission.wps[i].y_mm;
        int64_t az = mission.wps[i].z_mm;
        int64_t ux = (int64_t)mission.wps[i + 1].x_mm - ax;
        int64_t uy = (int64_t)mission.wps[i + 1].y_mm - ay;
        int64_t uz = (int64_t)mission.wps[i + 1].z_mm - az;

        int64_t wx = (int64_t)px - ax;
        int64_t wy = (int64_t)py - ay;
        int64_t wz = (int64_t)pz - az;

        __int128 len2 = (__int128)ux * ux + (__int128)uy * uy + (__int128)uz * uz;
        __int128 dot  = (__int128)wx * ux + (__int128)wy * uy + (__int128)wz * uz;

        if (len2 == 0 || dot <= 0) {
            __int128 d2 = (__int128)wx * wx + (__int128)wy * wy + (__int128)wz * wz;
            if (d2 <= r2)
                return 1;
        } else if (dot >= len2) {
            int64_t vx = (int64_t)px - mission.wps[i + 1].x_mm;
            int64_t vy = (int64_t)py - mission.wps[i + 1].y_mm;
            int64_t vz = (int64_t)pz - mission.wps[i + 1].z_mm;
            __int128 d2 = (__int128)vx * vx + (__int128)vy * vy + (__int128)vz * vz;
            if (d2 <= r2)
                return 1;
        } else {
            __int128 w2 = (__int128)wx * wx + (__int128)wy * wy + (__int128)wz * wz;
            if (w2 * len2 - dot * dot <= r2 * len2)
                return 1;
        }
    }
    return 0;
}

static int verify_mission(const struct gcs_mission *m)
{
    if (m->wp_count > MAX_WAYPOINTS ||
        m->fence_x_min > m->fence_x_max ||
        m->fence_y_min > m->fence_y_max ||
        m->fence_z_min > m->fence_z_max)
        return V_REJECT_FENCE;

    if (m->mission_seq <= mission.mission_seq)
        return V_REJECT_SEQ;

    uint8_t calc_hmac[32];
    hmac_sha256(K_GCS, sizeof(K_GCS),
                (const uint8_t *)m, offsetof(struct gcs_mission, hmac),
                calc_hmac);
    if (ct_memcmp32(calc_hmac, m->hmac) != 0)
        return V_REJECT_AUTH;

    mission.active             = 1;
    mission.mission_seq        = m->mission_seq;
    mission.fence_x_min        = m->fence_x_min;
    mission.fence_x_max        = m->fence_x_max;
    mission.fence_y_min        = m->fence_y_min;
    mission.fence_y_max        = m->fence_y_max;
    mission.fence_z_min        = m->fence_z_min;
    mission.fence_z_max        = m->fence_z_max;
    mission.v_max_mm_s         = m->v_max_mm_s;
    mission.corridor_radius_mm = m->corridor_radius_mm;
    mission.wp_count           = m->wp_count;
    for (uint32_t i = 0; i < m->wp_count; i++)
        mission.wps[i] = m->wps[i];

    return V_ACCEPT;
}

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

    /* 4. Geofence: the setpoint must be inside the box (and corridor if active). */
    int32_t fx_min = mission.active ? mission.fence_x_min : FENCE_X_MIN;
    int32_t fx_max = mission.active ? mission.fence_x_max : FENCE_X_MAX;
    int32_t fy_min = mission.active ? mission.fence_y_min : FENCE_Y_MIN;
    int32_t fy_max = mission.active ? mission.fence_y_max : FENCE_Y_MAX;
    int32_t fz_min = mission.active ? mission.fence_z_min : FENCE_Z_MIN;
    int32_t fz_max = mission.active ? mission.fence_z_max : FENCE_Z_MAX;
    uint32_t v_max = (mission.active && mission.v_max_mm_s > 0)
                     ? mission.v_max_mm_s : V_MAX_MM_S;

    if (c->x_mm < fx_min || c->x_mm > fx_max ||
        c->y_mm < fy_min || c->y_mm > fy_max ||
        c->z_mm < fz_min || c->z_mm > fz_max)
        return V_REJECT_FENCE;

    if (mission.active && mission.corridor_radius_mm > 0 && mission.wp_count >= 2) {
        if (!in_corridor(c->x_mm, c->y_mm, c->z_mm))
            return V_REJECT_FENCE;
    }

    /* 5 and 6. Step size, compared as squared distances so no sqrt is needed. */
    if (last.have_last) {
        int64_t dx = (int64_t)c->x_mm - last.x_mm;
        int64_t dy = (int64_t)c->y_mm - last.y_mm;
        int64_t dz = (int64_t)c->z_mm - last.z_mm;
        int64_t dist2 = dx * dx + dy * dy + dz * dz;

        /* 5. Teleport: larger than the biggest step ever allowed. */
        int64_t cap = (int64_t)v_max * DT_CAP_US / 1000000 + EPS_MM;
        if (dist2 > cap * cap)
            return V_REJECT_JUMP;

        /* 6. Speed: larger than v_max * dt + eps for the time that really passed. */
        if (dt_us < DT_CAP_US) {
            int64_t lim = (int64_t)v_max * (int64_t)dt_us / 1000000 + EPS_MM;
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
    if (req->req_type == REQ_MISSION)
        return verify_mission(&req->mission);

#ifdef VERIFIER_TEST_HOOKS
    if (req->cmd.mode == TEST_MODE_HANG)
        for (;;)
            ;
    if (req->cmd.mode == TEST_MODE_FAULT)
        return *(volatile long *)NORMAL_BASE;
#endif

    volatile struct actuator *act = (volatile struct actuator *)ACT_BASE;
    if (act->failsafe_count != last.failsafe_count) {
        last.mode = act->mode;
        last.failsafe_count = act->failsafe_count;
    }

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

    act->seq      = c->seq;
    act->x_mm     = c->x_mm;
    act->y_mm     = c->y_mm;
    act->z_mm     = c->z_mm;
    act->yaw_mdeg = c->yaw_mdeg;
    act->mode     = c->mode;
    act->accepted_count++;

    return V_ACCEPT;
}
