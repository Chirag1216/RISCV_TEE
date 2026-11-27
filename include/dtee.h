#ifndef DTEE_H
#define DTEE_H

/*
 * The secure-call interface, shared by the normal world, the monitor and the verifier.
 * All quantities are integers: the trusted world runs with the FPU off.
 */
#ifdef __KERNEL__
#include <linux/types.h>        /* inside the Linux kernel (the dtee driver) */
#else
#include <stdint.h>
#endif

/* ---- SBI calling convention: a7 = extension ID, a6 = function ID ---- */
/* Standard SBI extensions the monitor implements (enough to boot Linux) */
#define SBI_EID_SET_TIMER       0x00            /* legacy */
#define SBI_EID_PUTCHAR         0x01            /* legacy console putchar: a0 = character */
#define SBI_EID_GETCHAR         0x02            /* legacy */
#define SBI_EID_SHUTDOWN        0x08            /* legacy */
#define SBI_EID_BASE            0x10
#define SBI_EID_TIME            0x54494D45UL
#define SBI_EID_IPI             0x735049UL
#define SBI_EID_RFENCE          0x52464E43UL
#define SBI_EID_SRST            0x53525354UL

#define SBI_EID_DTEE            0x08000001UL    /* the secure call */
#define DTEE_FID_REGISTER       0               /* a0 = physical address of the shared page */
#define DTEE_FID_SUBMIT         1               /* verify the pose_cmd in the shared page   */
#define DTEE_FID_MISSION        2               /* verify & load signed gcs_mission         */

#define SBI_EID_DEBUG           0x08000002UL    /* test only */
#define DEBUG_FID_ACCEPTED      1               /* commands that reached the actuator    */
#define DEBUG_FID_FAILSAFE      2               /* times the monitor forced the safe state */

/* SBI error codes, returned in a0. A verdict is returned in a1.
 * (The Linux kernel already defines these in <asm/sbi.h>.) */
#ifndef __KERNEL__
#define SBI_SUCCESS             0
#define SBI_ERR_NOT_SUPPORTED   (-2L)
#define SBI_ERR_INVALID_PARAM   (-3L)
#define SBI_ERR_DENIED          (-4L)
#endif

/* ---- Flight modes ---- */
#define MODE_DISARMED   0
#define MODE_ARMED      1
#define MODE_FLIGHT     2
#define MODE_LAND       3
#define MODE_COUNT      4

/* ---- One command from the normal world ---- */
struct pose_cmd {
    uint64_t seq;           /* must increase with every command                  */
    uint64_t t_us;          /* sender's timestamp: informational, never trusted   */
    int32_t  x_mm;          /* position setpoint, millimetres                     */
    int32_t  y_mm;
    int32_t  z_mm;
    int32_t  yaw_mdeg;      /* heading, millidegrees                              */
    uint32_t mode;          /* MODE_*                                             */
    uint32_t reserved;
};

/* ---- Signed GCS mission corridor ---- */
#define MAX_WAYPOINTS   16

struct waypoint {
    int32_t x_mm;
    int32_t y_mm;
    int32_t z_mm;
};

struct gcs_mission {
    uint64_t mission_seq;
    int32_t  fence_x_min, fence_x_max;
    int32_t  fence_y_min, fence_y_max;
    int32_t  fence_z_min, fence_z_max;
    uint32_t v_max_mm_s;                /* 0 = keep default V_MAX_MM_S */
    uint32_t corridor_radius_mm;        /* 0 = box-only geofence */
    uint32_t wp_count;
    struct waypoint wps[MAX_WAYPOINTS];
    uint8_t  hmac[32];                  /* HMAC-SHA256 over all preceding bytes */
};

#define REQ_POSE        0
#define REQ_MISSION     1

/* ---- What the monitor hands the verifier (in the mailbox) ---- */
struct verifier_req {
    uint32_t req_type;      /* REQ_POSE or REQ_MISSION                            */
    uint64_t now_us;        /* trusted time, read by the monitor from the CLINT   */
    union {
        struct pose_cmd    cmd;
        struct gcs_mission mission;
    };
};

/* ---- Verdicts ---- */
#define V_ACCEPT        0
#define V_REJECT_SEQ    1   /* replayed or out-of-order sequence number           */
#define V_REJECT_RATE   2   /* commands arriving too fast                         */
#define V_REJECT_MODE   3   /* illegal mode or mode transition                    */
#define V_REJECT_FENCE  4   /* setpoint outside the geofence                      */
#define V_REJECT_JUMP   5   /* teleport: step larger than any allowed step        */
#define V_REJECT_SPEED  6   /* step too large for the time that has passed        */
#define V_TIMEOUT       7   /* verifier overran its time budget (set by monitor)  */
#define V_FAULT         8   /* verifier faulted or misbehaved (set by monitor)    */
#define V_REJECT_AUTH   9   /* mission HMAC authentication failed                 */
#define V_COUNT         10

/* ---- The mock actuator: a block of registers at ACT_BASE ---- */
struct actuator {
    uint64_t seq;               /* sequence number of the last accepted command   */
    int32_t  x_mm;
    int32_t  y_mm;
    int32_t  z_mm;
    int32_t  yaw_mdeg;
    uint32_t mode;
    uint32_t reserved;
    uint64_t accepted_count;    /* commands written by the verifier               */
    uint64_t failsafe_count;    /* safe-state commands written by the monitor     */
};

#endif
