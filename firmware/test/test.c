#include "pmgr_pram.h"
#include <stdio.h>
#include <stdint.h>
#include "eesim.h"
int fails=0;
#define CHECK(c,msg) do{ if(c) printf("  PASS  %s\n",msg); else {printf("  FAIL  %s\n",msg); fails++;} }while(0)
int main(void){
    for(int i=0;i<512;i++) EE[i]=0xFF;          /* erased EEPROM */
    pmgr_pram_init();
    bool v; uint8_t s = pmgr_pram_checksum(&v);
    CHECK(s==0 && !v, "blank PRAM: sum 0, reported INVALID (all-zero rule)");

    /* --- the captured reference: 3A 02 46 01 -> 3A 01 00 --- */
    CHECK(pmgr_pram_read(0x46)==0x00, "xPramRead loc $46 on blank -> 00  [matches capture]");

    /* write a byte, commit, checksum must now validate */
    pmgr_pram_write(0x46, 0x5A);
    pmgr_pram_commit();   /* returns at once; the save is background (2.156) */
    s = pmgr_pram_checksum(&v);
    CHECK(s==0x5A && v, "after write+commit: sum 0x5A, VALID");
    CHECK(pmgr_pram_read(0x46)==0x5A, "reads back 0x5A");

    /* corrupt the array behind the checksum -> must go invalid */
    pmgr_pram_write(0x10, 0x01);
    s = pmgr_pram_checksum(&v);
    CHECK(!v, "changed byte without commit -> INVALID");
    pmgr_pram_commit();
    s = pmgr_pram_checksum(&v);
    CHECK(v && s==0x5B, "recommit -> VALID, sum 0x5B");

    /* persistence across a power cycle - once the background save is durable */
    CHECK(pmgr_pram_save_pending(), "a save is pending after commit (nothing written yet)");
    pmgr_pram_flush();
    CHECK(!pmgr_pram_save_pending(), "flush completes the save");
    pmgr_pram_init();
    CHECK(pmgr_pram_read(0x46)==0x5A && pmgr_pram_read(0x10)==0x01,
          "survives re-init (EEPROM persistence)");
    pmgr_pram_checksum(&v);
    CHECK(v, "checksum still valid after reload");

    /* bounds: index >= 128 must not corrupt anything */
    pmgr_pram_write(200, 0xFF);
    CHECK(pmgr_pram_read(200)==0, "out-of-range write clamped");

    /* CombineTime */
    pmgr_time_set(0xAABBCCDDu);
    CHECK(pmgr_time_combine()==(uint8_t)(0xAA+0xBB+0xCC+0xDD), "CombineTime = sum of 4 bytes");
    pmgr_time_set(0xFFFFFFFFu); pmgr_time_tick();
    CHECK(pmgr_time_get()==0, "time wraps 32-bit");

    /* ---- time must SURVIVE a reset (pmgr.asm 157-159) --------------------
     * The original validates the time against its checksum at startup and
     * KEEPS it when they agree, because its RAM is never powered down. We do
     * the same with .noinit. An accidental `s_time = 0` in pmgr_pram_init()
     * would silently reintroduce the bug where every reset forgot the date,
     * so pin it here.
     *
     * (The mismatch branch - garbage .noinit after a real power loss - cannot
     * be reached from the host, where the statics are simply zeroed and the
     * checksum of 0 legitimately agrees.) */
    pmgr_time_set(0x12345678u);
    pmgr_pram_init();
    CHECK(pmgr_time_get()==0x12345678u, "time survives re-init (reset, checksum OK)");

    pmgr_time_tick();
    pmgr_pram_init();
    CHECK(pmgr_time_get()==0x12345679u, "tick refreshes the checksum, still survives");

    printf("\n  %s\n", fails? "FAILURES" : "all pass");
    return fails;
}
