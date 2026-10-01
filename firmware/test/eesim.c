/* Simulated EEPROM for the native PRAM tests (MEASUREMENTS 2.156, gate G1).
 *
 * Models what the saver depends on: an erase+write that stays BUSY for a
 * number of polls, commits its byte only when it finishes, and can be cut
 * short by a reset or brown-out that leaves an arbitrary (torn) value. It also
 * polices the saver's contract: never start a write while busy, never read
 * while busy, end the NVM command before the next write, at most one write
 * started per pmgr_pram_service() call. */
#include "eesim.h"
#include <stdio.h>
#include <stdlib.h>

uint8_t EE[512];
int  sim_busy_polls = 2;
long sim_started, sim_done, sim_violations, sim_reads;
static int  busy_left, pend_a = -1;
static uint8_t pend_v;
static bool cmd_active;
static int  stick_fail_a = -1;          /* next write to this address does not stick */
static bool stick_persistent;           /* ...and every later one too */
bool sim_busy_stuck;                    /* the NVM controller never finishes */

static void violation(const char *what) { if (sim_violations++ < 5) printf("  VIOLATION  %s\n", what); }

bool ee_busy(void)
{
    if (sim_busy_stuck) return true;
    if (busy_left > 0 && --busy_left == 0) {
        EE[pend_a] = (pend_a == stick_fail_a) ? (uint8_t)~pend_v : pend_v;
        if (pend_a == stick_fail_a && !stick_persistent) stick_fail_a = -1;
        pend_a = -1; sim_done++;
    }
    return busy_left > 0;
}
uint8_t ee_get(uint16_t a)
{
    if (a >= 512u) { violation("read out of range"); return 0; }
    if (busy_left > 0 || sim_busy_stuck) violation("read while busy");
    sim_reads++;
    return EE[a];
}
void ee_start_write(uint16_t a, uint8_t v)
{
    if (a >= 512u) { violation("write out of range"); return; }
    if (busy_left > 0) violation("write started while busy");
    if (cmd_active) violation("write started without ending the previous command");
    pend_a = a; pend_v = v; busy_left = sim_busy_polls; cmd_active = true; sim_started++;
}
void ee_end_write(void) { cmd_active = false; }

int sim_inflight(void) { return pend_a; }
void sim_reset(uint8_t torn)            /* reset / power loss NOW */
{
    if (pend_a >= 0) EE[pend_a] = torn;
    pend_a = -1; busy_left = 0; cmd_active = false;
}
void sim_erase(void) { for (int i = 0; i < 512; i++) EE[i] = 0xFF; sim_reset(0); }
void sim_stick_fail(int a) { stick_fail_a = a; stick_persistent = false; }
void sim_stick_fail_always(int a) { stick_fail_a = a; stick_persistent = (a >= 0); }
