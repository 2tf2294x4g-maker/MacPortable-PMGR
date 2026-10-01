/* Gate G1 (MEASUREMENTS 2.156): the background two-slot PRAM saver.
 *
 *   interrupted saves  - reset at EVERY write step of a save, with EVERY torn
 *                        value (0x00-0xFF) for the byte in flight - which
 *                        includes the old value (reset before the write) and
 *                        the new one (reset after it) -> boot loads exactly the old
 *                        or the new generation, never a mix; and a later save
 *                        still completes and loads
 *   concurrent changes - a host write injected at every step of a save -> the
 *                        final durable image equals the last write
 *   non-blocking       - at most one write started per service call; service
 *                        never waits on busy; never reads while busy
 *   seq wrap, both-invalid -> blank, verify-failure retry, wear per change */
#include "pmgr_pram.h"
#include "eesim.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails;
static long max_reads;         /* most EEPROM reads in any one service call */
#define CHECK(c, msg) do { if (!(c)) { printf("  FAIL  %s\n", msg); fails++; } } while (0)
#define PASS(msg) printf("  PASS  %s\n", msg)

typedef struct { uint8_t b[PRAM_SIZE]; bool valid; } img_t;

static img_t ram(void)
{
    img_t m; for (int i = 0; i < (int)PRAM_SIZE; i++) m.b[i] = pmgr_pram_read((uint8_t)i);
    pmgr_pram_checksum(&m.valid); return m;
}
static bool same(img_t a, img_t b) { return a.valid == b.valid && memcmp(a.b, b.b, PRAM_SIZE) == 0; }

/* one service call, policing "at most one write started" */
static void service1(void)
{
    long s0 = sim_started, r0 = sim_reads; pmgr_pram_service();
    if (sim_started - s0 > 1) { printf("  FAIL  more than one write started in one service call\n"); fails++; }
    if (sim_reads - r0 > max_reads) max_reads = sim_reads - r0;
}
static long run_idle(void)
{
    long calls = 0;
    while (pmgr_pram_save_pending()) { service1(); if (++calls > 100000) { printf("  FAIL  save never completes\n"); fails++; break; } }
    return calls;
}
static void apply(const uint8_t *idx, const uint8_t *val, int n)
{
    for (int i = 0; i < n; i++) pmgr_pram_write(idx[i], val[i]);
    pmgr_pram_commit();
}

/* Scenario = EEPROM state before the save + the change applied. */
typedef struct { const char *name; uint8_t ee[512]; img_t old; uint8_t idx[PRAM_SIZE]; uint8_t val[PRAM_SIZE]; int n; } scen_t;

static void load(const scen_t *sc) { memcpy(EE, sc->ee, 512); sim_reset(0); pmgr_pram_init(); }

static long count_writes(const scen_t *sc, img_t *new_out)
{
    load(sc); long s0 = sim_started;
    apply(sc->idx, sc->val, sc->n);
    *new_out = ram();
    run_idle();
    return sim_started - s0;
}

static void interruption_sweep(const scen_t *sc)
{
    img_t nw; long total = count_writes(sc, &nw);
    long cases = 0, got_old = 0, got_new = 0;
    for (long k = 1; k <= total; k++) {
        for (int t = 0; t < 256; t++) {
            load(sc); long s0 = sim_started;
            apply(sc->idx, sc->val, sc->n);
            for (long guard = 0; ; guard++) {
                if (sim_started - s0 == k && sim_inflight() >= 0) break;
                if (!pmgr_pram_save_pending() || guard > 100000) { printf("  FAIL  %s: step %ld never reached\n", sc->name, k); fails++; return; }
                service1();
            }
            sim_reset((uint8_t)t);
            pmgr_pram_init();                   /* reboot */
            img_t r = ram(); cases++;
            if (same(r, sc->old)) got_old++;
            else if (same(r, nw)) got_new++;
            else {
                printf("  FAIL  %s: reset at write %ld/%ld torn %d loaded a MIXED image\n", sc->name, k, total, t);
                fails++; return;
            }
            /* recovery: a further change must still become durable and load */
            pmgr_pram_write(0x7F, (uint8_t)(0xC3 ^ k ^ t)); pmgr_pram_commit();
            img_t want = ram(); run_idle(); pmgr_pram_init();
            if (!same(ram(), want)) { printf("  FAIL  %s: after reset at write %ld torn %d, the next save did not load\n", sc->name, k, t); fails++; return; }
        }
    }
    char m[200];
    snprintf(m, sizeof m, "%s: %ld writes/save, %ld resets (every step x 256 torn values) -> old %ld, new %ld, mixed 0",
             sc->name, total, cases, got_old, got_new);
    PASS(m);
}

static void concurrency_sweep(const scen_t *sc)
{
    img_t nw; long total = count_writes(sc, &nw);
    for (long k = 0; k <= total; k++) {
        load(sc); long s0 = sim_started;
        apply(sc->idx, sc->val, sc->n);
        while (pmgr_pram_save_pending() && sim_started - s0 < k) service1();
        pmgr_pram_write(0x05, (uint8_t)(0x80 | k)); pmgr_pram_write(0x40, (uint8_t)~k); pmgr_pram_commit();
        img_t last = ram();
        run_idle();
        pmgr_pram_init();
        if (!same(ram(), last)) { printf("  FAIL  %s: write injected at step %ld was lost\n", sc->name, k); fails++; return; }
    }
    char m[160]; snprintf(m, sizeof m, "%s: host write injected at each of %ld steps -> last write durable every time", sc->name, total + 1);
    PASS(m);
}

static void snapshot(scen_t *sc) { memcpy(sc->ee, EE, 512); pmgr_pram_init(); sc->old = ram(); }

int main(void)
{
    static scen_t sc;

    /* ---- basics --------------------------------------------------------- */
    sim_erase(); pmgr_pram_init();
    { bool v; pmgr_pram_checksum(&v); CHECK(!v && !pmgr_pram_save_pending(), "blank EEPROM -> blank PRAM, nothing pending"); }
    pmgr_pram_write(0x10, 0x42); pmgr_pram_commit();
    CHECK(sim_started == 0, "commit writes NOTHING to EEPROM itself");
    CHECK(pmgr_pram_read(0x10) == 0x42, "RAM serves the new value immediately");
    sim_busy_polls = 50;
    { long c = run_idle(); CHECK(c > 50, "service returns while busy (many calls per write)"); }
    sim_busy_polls = 2;
    CHECK(pmgr_pram_save_count() == 1, "one completed save");
    { img_t want = ram(); pmgr_pram_init(); CHECK(same(ram(), want), "reload after save"); }
    PASS("basics: commit is non-blocking, service never waits, save completes and reloads");

    /* both slots damaged -> blank */
    { EE[0] ^= 1; pmgr_pram_init(); bool v; pmgr_pram_checksum(&v);
      CHECK(!v && pmgr_pram_read(0x10) == 0, "damaged only slot -> blank PRAM"); }
    PASS("damaged slot -> blank PRAM (the Mac initialises it)");

    /* ---- scenario A: first save onto erased EEPROM ----------------------- */
    sim_erase(); snapshot(&sc); sc.name = "A first save, erased EEPROM";
    { const uint8_t i[] = {0x00, 0x10, 0x11, 0x46, 0x7E}, v[] = {0xA8, 0x01, 0x84, 0x5A, 0x33};
      memcpy(sc.idx, i, 5); memcpy(sc.val, v, 5); sc.n = 5; }
    interruption_sweep(&sc); concurrency_sweep(&sc);

    /* ---- scenario B: one valid slot, the other erased -------------------- */
    sim_erase(); pmgr_pram_init(); apply(sc.idx, sc.val, 5); run_idle();
    snapshot(&sc); sc.name = "B second save, other slot erased";
    { const uint8_t i[] = {0x10, 0x20, 0x46}, v[] = {0x02, 0x99, 0x5B}; memcpy(sc.idx, i, 3); memcpy(sc.val, v, 3); sc.n = 3; }
    interruption_sweep(&sc); concurrency_sweep(&sc);

    /* ---- scenario C: both slots valid, target holds an OLDER generation --- */
    apply(sc.idx, sc.val, 3); run_idle();       /* now gen2 active, gen1 in the other slot */
    snapshot(&sc); sc.name = "C steady state, target two generations old";
    { const uint8_t i[] = {0x10}, v[] = {0x03}; memcpy(sc.idx, i, 1); memcpy(sc.val, v, 1); sc.n = 1; }
    interruption_sweep(&sc); concurrency_sweep(&sc);
    { img_t d; long w = count_writes(&sc, &d);
      char m[120]; snprintf(m, sizeof m, "wear: one-byte change in steady state = %ld EEPROM writes (~%ld ms)", w, w * 10);
      CHECK(w <= 8, "one-byte change costs <= 8 writes"); PASS(m); }

    /* ---- scenario D: every byte changes ---------------------------------- */
    snapshot(&sc); sc.name = "D all 128 bytes change";
    for (int i = 0; i < (int)PRAM_SIZE; i++) { sc.idx[i] = (uint8_t)i; sc.val[i] = (uint8_t)(i * 7 + 1); }
    sc.n = PRAM_SIZE;
    interruption_sweep(&sc); concurrency_sweep(&sc);

    /* ---- scenario E: seq wrap ------------------------------------------- */
    for (int g = 0; g < 600; g++) { pmgr_pram_write(0x60, (uint8_t)g); pmgr_pram_write(0x61, (uint8_t)(g >> 8)); pmgr_pram_commit(); run_idle();
        pmgr_pram_init();
        if (pmgr_pram_read(0x60) != (uint8_t)g || pmgr_pram_read(0x61) != (uint8_t)(g >> 8)) { printf("  FAIL  seq wrap: generation %d not the one loaded\n", g); fails++; break; }
        if (EE[0] == 0xFE || EE[132] == 0xFE) { snapshot(&sc); }   /* the next save wraps FE -> 00 */
    }
    PASS("seq wrap: 600 consecutive saves (seq wraps twice), the newest loads every time");
    sc.name = "E save across the seq wrap";
    { const uint8_t i[] = {0x60, 0x61, 0x62}, v[] = {0xEE, 0xDD, 0xCC}; memcpy(sc.idx, i, 3); memcpy(sc.val, v, 3); sc.n = 3; }
    CHECK(sc.ee[0] == 0xFE || sc.ee[132] == 0xFE, "scenario E starts with an active seq of 0xFE");
    interruption_sweep(&sc); concurrency_sweep(&sc);

    /* ---- verify failure -> retry ---------------------------------------- */
    sim_erase(); pmgr_pram_init(); apply((const uint8_t[]){0x10}, (const uint8_t[]){0x55}, 1); run_idle();
    pmgr_pram_write(0x10, 0x66); pmgr_pram_commit();
    sim_stick_fail(132 + 1 + 0x10);             /* the data byte in the target slot */
    { img_t want = ram(); run_idle(); pmgr_pram_init();
      CHECK(same(ram(), want), "a write that did not stick is detected and the save retried"); }
    PASS("verify failure: detected at read-back, save retried, newest loads");

    /* ---- flush ------------------------------------------------------------ */
    pmgr_pram_write(0x22, 0x77); pmgr_pram_commit(); pmgr_pram_flush();
    CHECK(!pmgr_pram_save_pending(), "flush leaves nothing pending");
    { img_t want = ram(); sim_reset(0); pmgr_pram_init(); CHECK(same(ram(), want), "flushed change survives an immediate power loss"); }
    PASS("flush: durable on return");

    /* ---- failure policy (review, MEASUREMENTS 2.184): every loop bounded ---- */
    sim_erase(); pmgr_pram_init(); apply((const uint8_t[]){0x10}, (const uint8_t[]){0x11}, 1); pmgr_pram_flush();
    { img_t good = ram();
      pmgr_pram_write(0x10, 0x22); pmgr_pram_commit();
      sim_stick_fail_always(132 + 1 + 0x10);       /* the target slot's byte never sticks */
      long w0 = sim_started;
      bool durable = pmgr_pram_flush();
      long w = sim_started - w0;
      CHECK(!durable && !pmgr_pram_save_pending() && pmgr_pram_save_failed(), "persistent write failure: flush RETURNS false, nothing pending, failure latched");
      char m[140]; snprintf(m, sizeof m, "persistent write failure: gave up after %ld EEPROM writes (3 attempts), not forever", w);
      CHECK(w < 3 * 140, m); PASS(m);
      CHECK(pmgr_pram_read(0x10) == 0x22, "  ...RAM keeps the new setting while powered");
      sim_reset(0); pmgr_pram_init();
      CHECK(same(ram(), good), "  ...after power loss the previous valid image loads");
      /* background saver also stops (no flush) */
      pmgr_pram_write(0x10, 0x33); pmgr_pram_commit(); w0 = sim_started; long calls = run_idle();
      CHECK(!pmgr_pram_save_pending() && pmgr_pram_save_failed() && sim_started - w0 < 3 * 140 && calls < 100000,
            "persistent write failure without flush: the background saver stops too (no endless rewriting)");
      /* the fault clears; the next host change starts a fresh series and succeeds */
      sim_stick_fail_always(-1);
      pmgr_pram_write(0x10, 0x44); pmgr_pram_commit();
      CHECK(pmgr_pram_flush() && !pmgr_pram_save_failed(), "fault cleared: the next change saves and the failure flag clears");
      { img_t want = ram(); sim_reset(0); pmgr_pram_init(); CHECK(same(ram(), want), "  ...and it loads after power loss"); }
      PASS("failure policy: persistent read-back failure is bounded and recoverable");
    }
    /* stuck NVM controller during a flush, and at boot */
    { pmgr_pram_write(0x11, 0x55); pmgr_pram_commit();
      sim_busy_stuck = true;
      bool durable = pmgr_pram_flush();
      CHECK(!durable && !pmgr_pram_save_pending() && pmgr_pram_save_failed(), "EEBUSY stuck: flush gives up after its poll budget and returns");
      pmgr_pram_init();
      bool v; pmgr_pram_checksum(&v);
      CHECK(!v && pmgr_pram_save_failed(), "EEBUSY stuck at boot: init returns (bounded), slots not trusted, PRAM blank, failure latched");
      sim_busy_stuck = false; sim_reset(0);
      pmgr_pram_init();
      CHECK(!pmgr_pram_save_failed() && pmgr_pram_read(0x10) == 0x44, "  ...controller recovers: next boot loads the last good image");
      PASS("failure policy: a stuck NVM controller cannot hang flush or boot");
    }

    { char m[120]; snprintf(m, sizeof m, "bounded work: at most %ld EEPROM reads in any one service call (budget 16 + 3)", max_reads);
      CHECK(max_reads <= 19, "service call work stays within its budget"); PASS(m); }
    CHECK(sim_violations == 0, "simulator contract: no read/write while busy, command ended before the next write");
    printf("\n  simulator contract violations: %ld\n", sim_violations);
    printf("  %s\n", fails ? "FAILURES" : "all pass");
    return fails != 0;
}
