/* MEASUREMENTS 2.445 - the park's PIT fallback must never fire inside a live SIXTYHZ period.
 *
 * Runs the REAL park_sleep_until_edge() (handlers_native.c, included so the static function is reachable) against a
 * simulated clock: SIXTYHZ falling edges every 1/f60 s, the RTC PIT free-running every 1/fpit s at an arbitrary phase.
 * sleep_cpu() is hooked to advance time to the next event and set the wake cause, as the two ISRs do while parked.
 *
 * The shipped fallback (2nd PIT interrupt) counted ~3 % too many ticks; with SIXTYHZ present the tick count must equal
 * the edge count for EVERY PIT phase, wake-to-re-arm time and oscillator error swept here. With SIXTYHZ absent the
 * fallback must still fire, after 31.25-46.9 ms (two to three whole PIT periods). */
#define main handlers_main_unused
#include "handlers_native.c"
#undef main
#include <stdio.h>
#include <stdlib.h>

static int64_t now_ns, edge_period_ns, pit_period_ns, pit_phase_ns;   /* edges at k*edge_period; PIT at phase + k*period */
static bool edges_on;
static int failures;

static int64_t next_after(int64_t t, int64_t phase, int64_t period)
{
    int64_t k = (t - phase) / period;
    int64_t at = phase + k * period;
    while (at <= t) at += period;
    return at;
}

void mock_sleep_cpu(void)
{
    int64_t pit = next_after(now_ns, pit_phase_ns, pit_period_ns);
    int64_t edge = edges_on ? next_after(now_ns, 0, edge_period_ns) : INT64_MAX;
    if (edge <= pit) { now_ns = edge; g_park_edge = true; }     /* PORTC_PORT_vect while g_parked */
    else             { now_ns = pit;  g_park_pit  = true; }     /* RTC_PIT_vect while g_parked */
}

static void check(bool ok, const char *what)
{
    if (!ok) { failures++; if (failures <= 10) printf("  FAIL %s\n", what); }
}

int main(void)
{
    RTC.PITCTRLA = RTC_PITEN_bm;                                  /* the PIT path, as on a board whose RTC started */
    const double f60s[]  = { 59.5, 60.0, 60.2, 60.5 };
    const double fpits[] = { 64.0 * 0.95, 64.0, 64.0 * 1.003, 64.0 * 1.05 };   /* OSC32K is a few % at best */
    const int64_t reproc_ns[] = { 0, 50000, 100000, 300000, 600000, 1000000, 3000000 };
    long cases = 0;

    /* 1. SIXTYHZ present: every park return must be an edge, never the fallback. */
    for (unsigned a = 0; a < sizeof f60s / sizeof *f60s; a++)
    for (unsigned b = 0; b < sizeof fpits / sizeof *fpits; b++)
    for (unsigned c = 0; c < sizeof reproc_ns / sizeof *reproc_ns; c++)
    for (int ph = 0; ph < 64; ph++) {
        edge_period_ns = (int64_t)(1e9 / f60s[a]);
        pit_period_ns  = (int64_t)(1e9 / fpits[b]);
        pit_phase_ns   = pit_period_ns * ph / 64;
        edges_on = true; now_ns = 1;
        long edges_seen = 0, fallbacks = 0;
        int64_t start = now_ns;
        while (now_ns - start < 20LL * 1000000000LL) {              /* 20 simulated seconds */
            park_wake_t w = park_sleep_until_edge();
            if (w == PARK_EDGE) edges_seen++; else fallbacks++;
            now_ns += reproc_ns[c];                                 /* the tick's own work before the next park */
        }
        cases++;
        char msg[160];
        snprintf(msg, sizeof msg, "60Hz=%.1f PIT=%.2f reproc=%lld us phase %d/64: %ld fallback ticks with SIXTYHZ live",
                 f60s[a], fpits[b], (long long)reproc_ns[c] / 1000, ph, fallbacks);
        check(fallbacks == 0, msg);
        (void)edges_seen;
    }

    /* 2. SIXTYHZ absent: the fallback still fires, after two to three whole PIT periods. */
    for (unsigned b = 0; b < sizeof fpits / sizeof *fpits; b++)
    for (int ph = 0; ph < 64; ph++) {
        pit_period_ns = (int64_t)(1e9 / fpits[b]);
        pit_phase_ns  = pit_period_ns * ph / 64;
        edges_on = false; now_ns = 12345;
        int64_t t0 = now_ns;
        park_wake_t w = park_sleep_until_edge();
        int64_t dt = now_ns - t0;
        cases++;
        char msg[160];
        snprintf(msg, sizeof msg, "no SIXTYHZ, PIT=%.2f phase %d/64: wake %d after %lld us", fpits[b], ph, (int)w,
                 (long long)dt / 1000);
        check(w == PARK_PIT_FALLBACK && dt >= 2 * pit_period_ns && dt <= 3 * pit_period_ns, msg);
    }

    printf("test_park: %ld cases, %d failure(s)\n", cases, failures);
    return failures ? 1 : 0;
}
