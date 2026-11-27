#ifndef SBI_H
#define SBI_H

#include "trap.h"

/* Handle an ecall from S-mode. Arguments and results live in the trap frame. */
void sbi_handle(struct trap_frame *tf);

#endif
