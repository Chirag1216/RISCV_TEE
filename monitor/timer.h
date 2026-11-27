#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

uint64_t time_now_us(void);             /* trusted clock, microseconds since boot      */
void watchdog_arm(uint64_t budget_us);  /* machine timer interrupt after budget_us     */
void watchdog_disarm(void);

#endif
