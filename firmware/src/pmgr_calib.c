/* Per-unit ADC calibration record. See pmgr_calib.h for why the GATE, not the
 * fit, is the safety-critical part. Pre-registered MEASUREMENTS.md 2.349.  */
#include "pmgr_calib.h"
#include <string.h>

/* ---- EEPROM read ---------------------------------------------------------
 * Mirrors pmgr_pram.c's split exactly: inline on the AVR, the simulator's HAL
 * natively. ⛔ READ-ONLY here. Writing the record is the INSTALLER's job over
 * UPDI, not the firmware's - §6: every FLASH erases EEPROM, so the record is
 * written AFTER the final image, and firmware that could write it would be one
 * more way to lose or corrupt it. */
#ifdef __AVR__
#include <avr/io.h>
static inline uint8_t ee_get(uint16_t a) { return *(uint8_t *)(MAPPED_EEPROM_START + a); }
#else
#include "pmgr_ee_hal.h"
#endif

static pmgr_calib_t s_rec;
static bool         s_valid;
static bool         s_gated;   /* latched per LOAD - see pmgr_calib_load() */

/* CRC-16/CCITT reflected - byte-for-byte pmgr_pram.c's crc_update().
 *
 * ⛔ CORRECTED 2026-09-28 BEFORE ANYTHING WAS WRITTEN TO A BOARD. This first
 * carried a DIFFERENT algorithm (CRC-16/ARC, poly 0xA001) under a comment
 * claiming it was "the SAME routine pmgr_pram.c uses ... kept byte-identical
 * deliberately". It was not, and the stated rationale - "two CRCs in one image
 * is two things to get wrong" - was being violated by the very code asserting
 * it. Both are sound CRCs and the record validated correctly either way, so
 * nothing was broken; the CLAIM was false, and the image really did carry two
 * CRC algorithms. Now it carries one, and the PRAM CRC is already exercised by
 * test_save.c's torn-write cases. */
static uint16_t crc_update(uint16_t crc, uint8_t d)
{
    d ^= (uint8_t)crc;
    d ^= (uint8_t)(d << 4);
    return (uint16_t)((((uint16_t)d << 8) | (crc >> 8)) ^ (uint8_t)(d >> 4) ^ ((uint16_t)d << 3));
}

uint16_t pmgr_calib_crc(const uint8_t *b, uint8_t n)
{
    uint16_t c = 0xFFFFu;
    for (uint8_t i = 0; i < n; i++) c = crc_update(c, b[i]);
    return c;
}

/* Little-endian throughout, matching the AVR's own byte order so the installer
 * tool and the firmware cannot disagree about it. */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static void     wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

void pmgr_calib_encode(const pmgr_calib_t *c, uint8_t *o)
{
    wr16(o + 0, c->magic);
    o[2] = c->version;
    o[3] = c->pairing_id;
    wr16(o + 4, c->a_q8);
    wr16(o + 6, (uint16_t)c->b_q8);
    o[8] = c->resid_q4;
    wr16(o + 9,  c->span_mv);
    wr16(o + 11, c->rail_mv);
    o[13] = c->reserved;
    wr16(o + 14, pmgr_calib_crc(o, 14u));
}

/* ⛔ EVERY bound is checked here, not at the call sites. A caller that forgets
 * one would silently enable fast charge on a bad record, which is the exact
 * failure this file exists to prevent. */
bool pmgr_calib_decode(const uint8_t *in, pmgr_calib_t *c)
{
    c->magic      = rd16(in + 0);
    c->version    = in[2];
    c->pairing_id = in[3];
    c->a_q8       = rd16(in + 4);
    c->b_q8       = (int16_t)rd16(in + 6);
    c->resid_q4   = in[8];
    c->span_mv    = rd16(in + 9);
    c->rail_mv    = rd16(in + 11);
    c->reserved   = in[13];
    c->crc        = rd16(in + 14);

    if (c->magic   != PMGR_CALIB_MAGIC)     return false;   /* N1.4 absent, N1.5 torn */
    if (c->version != PMGR_CALIB_VERSION)   return false;
    if (c->crc     != pmgr_calib_crc(in, 14u)) return false; /* N1.3 */
    if (c->a_q8 < PMGR_CALIB_A_MIN || c->a_q8 > PMGR_CALIB_A_MAX) return false;  /* N1.6 */
    if (c->b_q8 >  PMGR_CALIB_B_ABS_MAX ||
        c->b_q8 < -PMGR_CALIB_B_ABS_MAX)    return false;
    if (c->resid_q4 > PMGR_CALIB_RESID_MAX) return false;   /* N1.7 - 2.338's limit */
    if (c->span_mv  < PMGR_CALIB_SPAN_MIN)  return false;   /* N1.8 - 2.339's limit */
    return true;
}

bool pmgr_calib_load(void)
{
    uint8_t b[PMGR_CALIB_SIZE];
    for (uint8_t i = 0; i < PMGR_CALIB_SIZE; i++)
        b[i] = ee_get((uint16_t)(PMGR_CALIB_EE_BASE + i));
    s_valid = pmgr_calib_decode(b, &s_rec);
    if (!s_valid) memset(&s_rec, 0, sizeof s_rec);
    /* ⭐ THE LATCH IS SCOPED TO A LOAD, WHICH ON HARDWARE MEANS A BOOT. Clearing
     * it here - and NOWHERE else - is what "latched for the session" means, and
     * it is why no public reset exists: a safety latch that anything can clear
     * cannot answer "did this ever happen?". ⚠️ It also makes the native tests
     * deterministic, which a process-global latch otherwise is not. */
    s_gated = false;
    return s_valid;
}

bool pmgr_calib_valid(void) { return s_valid; }


void pmgr_calib_note_gated(void) { s_gated = true; }

/* ⚠️ GATED IS LATCHED, UNCAL IS LIVE. The distinction is deliberate: "there is
 * no record" is a current fact, while "the gate has fired" is a history that
 * must not be erased by the machine later settling into a state that looks
 * ordinary. A flag that clears itself cannot answer "did this ever happen?" */
uint8_t pmgr_calib_status(void)
{
    uint8_t v = 0u;
    if (!s_valid) v |= PMGR_CALIB_ST_UNCAL;
    if (s_gated)  v |= PMGR_CALIB_ST_GATED;
    return v;
}

uint8_t pmgr_calib_pairing(void) { return s_valid ? s_rec.pairing_id : 0u; }

/* raw_unit = a*raw_nom/100 + b, in Q8:
 *     = (a_q8*raw_nom + b_q8*100) / 25600, rounded half-up.
 * ⭐ THE THRESHOLDS ARE CONVERTED, NOT THE READINGS (2.349): thresholds are few
 * and fixed, readings are continuous, and this keeps every comparison integer.
 * Check, knee: (25434*720 - 5955*100 + 12800)/25600 = 692 -> stored level 180,
 * against 2.347's MEASURED 179.36 at 7.1994 V. */
uint16_t pmgr_calib_threshold(uint16_t raw_nominal)
{
    if (!s_valid) return raw_nominal;
    int32_t n = (int32_t)s_rec.a_q8 * (int32_t)raw_nominal
              + (int32_t)s_rec.b_q8 * 100 + 12800;
    if (n < 0) return 0u;
    n /= 25600;
    return (n > 1023) ? 1023u : (uint16_t)n;
}

/* The inverse, for the level the Mac sees (§5). raw_nom = 100*(raw_unit - b)/a. */
uint8_t pmgr_calib_to_nominal(uint8_t level_measured)
{
    if (!s_valid) return level_measured;
    int32_t raw_unit = (int32_t)level_measured + 512;
    int32_t n = (raw_unit * 25600 - (int32_t)s_rec.b_q8 * 100);
    n /= (int32_t)s_rec.a_q8;                 /* = 100*(raw_unit - b)/a */
    n -= 512;
    if (n < 0)   return 0u;
    if (n > 255) return 255u;
    return (uint8_t)n;
}
