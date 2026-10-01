#ifndef PMGR_TICK_DEFER_H
#define PMGR_TICK_DEFER_H
/* Fix A (design/ADB-HOST-SERVICE.md §10, MEASUREMENTS 2.209): the /60 second divider when a host command byte was
 * latched during this tick's ADB work. The divider always advances - no second is lost - but the 1 Hz work is
 * DEFERRED until the latched transaction has been served from the main loop. Returns true when pmgr_tick_1s()
 * should run now. Header-only so the native suite can test it without pmgr_handlers.c. */
#include <stdbool.h>
#include <stdint.h>

static inline bool pmgr_tick_divider_step(uint8_t *divider, bool latched, bool *deferred_1s)
{
    if (--*divider != 0u) return false;
    *divider = 60u;
    if (latched) { *deferred_1s = true; return false; }
    return true;
}
#endif
