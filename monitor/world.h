#ifndef WORLD_H
#define WORLD_H

#include <stdint.h>
#include "trap.h"

void world_init(void);                              /* load the verifier into trusted memory */
long dtee_register(uint64_t shared_pa);             /* normal world registers its shared page */
long dtee_submit(long *verdict);                    /* run the verifier on the shared page    */
void trusted_trap(struct trap_frame *tf, uint64_t mcause);   /* any trap while the verifier runs */
int  in_trusted_world(void);

uint64_t actuator_accepted_count(void);
uint64_t actuator_failsafe_count(void);

#endif
