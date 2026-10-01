#ifndef EESIM_H
#define EESIM_H
#include "pmgr_ee_hal.h"
extern uint8_t EE[512];
extern int  sim_busy_polls;
extern long sim_started, sim_done, sim_violations, sim_reads;
int  sim_inflight(void);
void sim_reset(uint8_t torn);
void sim_erase(void);
void sim_stick_fail(int a);
void sim_stick_fail_always(int a);   /* -1 clears */
extern bool sim_busy_stuck;
#endif
