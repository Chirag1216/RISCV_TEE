#ifndef TRAP_H
#define TRAP_H

#include <stdint.h>

/* Layout must match trap.S: regs[i] holds register x_i (regs[0] unused). */
struct trap_frame {
    uint64_t regs[32];
};

/* Indices into regs[] for the registers the monitor reads by name. */
#define REG_A0  10
#define REG_A1  11
#define REG_A6  16
#define REG_A7  17

void trap_init(void);
void trap_handler(struct trap_frame *tf);

#endif
