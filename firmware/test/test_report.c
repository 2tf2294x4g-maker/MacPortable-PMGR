/* 2.411/2.413: the reported-level remap. ORIGINAL (or no record) must be the identity; RECREATION the map. */
#include <stdio.h>
#include "pmgr_report.h"
static int fails;
static void ok(int c, const char *m) { printf("  %s  %s\n", c ? "PASS" : "FAIL", m); if (!c) fails++; }
int main(void) {
    int id = 1;
    for (int n = 0; n < 256; n++) if (pmgr_report_level((uint8_t)n, false) != n) id = 0;
    ok(id, "original hybrid: identity over 0..255");
    ok(pmgr_report_level(108, true) == 78,  "recreation LOW 108 -> Apple LOW 78");
    ok(pmgr_report_level(107, true) <  78,  "one below recreation LOW is below Apple LOW (OS warns)");
    ok(pmgr_report_level(93, true)  == 62,  "recreation DEAD 93 -> Apple DEAD 62");
    ok(pmgr_report_level(100, true) >= 69 && pmgr_report_level(100, true) <= 70, "recreation STAY 100 -> ~Apple STAY 70");
    ok(pmgr_report_level(208, true) == 208 && pmgr_report_level(255, true) == 255, "knee and above unchanged (full reads full)");
    ok(pmgr_report_level(0, true) == 0, "floor at 0");
    int mono = 1, cont = 1;
    for (int n = 1; n < 256; n++) {
        int a = pmgr_report_level((uint8_t)(n - 1), true), b = pmgr_report_level((uint8_t)n, true);
        if (b < a) mono = 0;
        if (b - a > 2) cont = 0;
    }
    ok(mono, "recreation: monotonic over 0..255");
    ok(cont, "recreation: no jump larger than 2 counts anywhere");
    printf("  %d failure(s)\n", fails);
    return fails != 0;
}
