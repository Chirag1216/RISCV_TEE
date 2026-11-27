#ifndef DTEE_IOCTL_H
#define DTEE_IOCTL_H

/*
 * The interface between Linux user programs and the /dev/dtee driver.
 * Used by the driver (kernel) and by user programs.
 */
#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif
#include "dtee.h"

/* Submit one command. The driver fills in `verdict` (V_ACCEPT, V_REJECT_*, ...). */
struct dtee_submit {
    struct pose_cmd cmd;
    uint32_t verdict;
    uint32_t reserved;
};

/* Counters kept by the monitor. */
struct dtee_stats {
    uint64_t accepted;      /* commands that reached the actuator          */
    uint64_t failsafe;      /* times the monitor forced the safe state     */
};

#define DTEE_IOC_SUBMIT       _IOWR('d', 1, struct dtee_submit)
#define DTEE_IOC_STATS        _IOR('d', 2, struct dtee_stats)

/* Test only: the kernel itself tries to write the actuator.
 * Result: 1 = the write was blocked (expected), 0 = it went through. */
#define DTEE_IOC_TEST_BYPASS  _IOR('d', 3, int)

#endif
