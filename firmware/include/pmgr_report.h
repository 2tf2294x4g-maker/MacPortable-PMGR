/* The battery level REPORTED TO THE MAC ($68/$69 reply) - MEASUREMENTS 2.411.
 *
 * ORIGINAL hybrid (and no valid record): the identity - the Mac sees the calibrated nominal level, as always.
 *
 * RECREATION hybrid (calibration record's hybrid field, 2.413; was the `HYBRID=recreation` build in 2.411): the PMGR's own thresholds sit higher (LOW 108, STAY_ASLEEP 100, DEAD 93 - pmgr_adc.h) because a
 * recreation hybrid cuts power at ~5.92 V, above Apple's DEAD. 2.409/2.410 showed the Mac does not follow the PMGR's flags
 * alone: its bars and its own warning work from the LEVEL on Apple's scale (LOW 78, DEAD 62), so it never saw "low", put up
 * no warning, and was caught by the PMGR's DEAD backstop sleep, after which a wake restarted (2/2). So the level the Mac
 * sees is remapped, piecewise-linearly, so Apple's scale lands on the recreation thresholds:
 *
 *     nominal   >= 208 (the knee and above)   unchanged             a charged pack still reads full
 *     108..208                                78 + (n-108)*130/100  108 -> 78 (Apple LOW)   208 -> 208
 *      93..108                                62 + (n- 93)* 16/ 15   93 -> 62 (Apple DEAD)  100 -> 69 (~Apple STAY 70)
 *     below 93                                n - 31 (floor 0)      continuous at 93
 *
 * Monotonic and continuous. ⛔ Only the REPORTED value changes: every decision the PMGR makes - LOW/STAY_ASLEEP/DEAD,
 * the knee, HICHG - still uses the real level. */
#ifndef PMGR_REPORT_H
#define PMGR_REPORT_H

#include <stdint.h>
#include <stdbool.h>
#include "pmgr_adc.h"

#define PMGR_APPLE_LOW_LEVEL   78u
#define PMGR_APPLE_DEAD_LEVEL  62u
#define PMGR_REPORT_TOP_LEVEL 208u      /* the knee literal - remap stops here */

static inline uint8_t pmgr_report_level(uint8_t n, bool recreation)
{
    if (!recreation) return n;
    const uint16_t lo = PMGR_RECREATION_LOW_LEVEL, de = PMGR_RECREATION_DEAD_LEVEL, top = PMGR_REPORT_TOP_LEVEL;
    if (n >= top) return n;
    if (n >= lo)  return (uint8_t)(PMGR_APPLE_LOW_LEVEL +
                          ((uint16_t)(n - lo) * (top - PMGR_APPLE_LOW_LEVEL) + (top - lo) / 2u) / (top - lo));
    if (n >= de)  return (uint8_t)(PMGR_APPLE_DEAD_LEVEL +
                          ((uint16_t)(n - de) * (PMGR_APPLE_LOW_LEVEL - PMGR_APPLE_DEAD_LEVEL) + (lo - de) / 2u) / (lo - de));
    return (n > (de - PMGR_APPLE_DEAD_LEVEL)) ? (uint8_t)(n - (de - PMGR_APPLE_DEAD_LEVEL)) : 0u;
}

#endif
