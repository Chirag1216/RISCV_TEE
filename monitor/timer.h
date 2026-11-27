#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

uint64_t time_now_us(void);                     /* trusted clock, microseconds since boot      */
uint64_t time_now_ticks(void);                  /* raw CLINT mtime ticks (10 MHz)              */
void watchdog_arm(uint64_t budget_us);          /* machine timer interrupt after budget_us     */
void watchdog_arm_deadline(uint64_t deadline_us); /* machine timer interrupt at deadline_us    */
void watchdog_disarm(void);

#endif
