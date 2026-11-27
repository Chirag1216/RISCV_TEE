#ifndef PMP_H
#define PMP_H

/* Which world is about to run. The view decides what S/U-mode code may touch. */
enum pmp_view {
    PMP_VIEW_NORMAL,    /* Linux: all memory except the monitor and the trusted regions */
    PMP_VIEW_TRUSTED,   /* verifier: only its own code, data and the actuator           */
};

int  pmp_probe(void);                   /* count implemented PMP entries and print it */
void pmp_init(void);                    /* program the region addresses, normal view  */
void pmp_set_view(enum pmp_view view);  /* switch permissions between the two worlds  */

#endif
