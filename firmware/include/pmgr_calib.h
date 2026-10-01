/* Per-unit ADC calibration record — design/HYBRID-CALIBRATION.md §5, pre-registered
 * in MEASUREMENTS.md 2.349, measured in 2.347.
 *
 * ⭐⭐ WHY THIS EXISTS, AND WHY THE GATE MATTERS MORE THAN THE FIT.
 * Apple's overflow guard only raises a FLAG. ⛔ If the knee is never reached,
 * NOTHING in the PMGR ends high charge (MEASUREMENTS 2.246). 2.347 measured
 * exactly that failure on #1: at 7.1994 V TRUE the board reports level 179
 * against a knee literal of 208, so an UNCALIBRATED pairing would charge on and
 * never terminate. The gate below is the only identified firmware mechanism that
 * prevents it.
 *
 * ⛔⛔ WHAT THE GATE DOES NOT DO. Falling back to state 3 is Apple's own float
 * regime, and it removes the dependence on the PMGR's reading. It does NOT make
 * the state safe: state 3's voltage is set by the HYBRID, not by us, and #1's
 * recreation hybrid's float voltage is UNMEASURED. Never rely on it unattended
 * before B3 (§7).
 *
 * THE RECORD lives at EEPROM 464, clear of PRAM's two 132-byte slots (0-263) and
 * the parked recorder (264-463). 464 + 16 = 480, inside the 512-byte EEPROM.
 */
#ifndef PMGR_CALIB_H
#define PMGR_CALIB_H

#include <stdint.h>
#include <stdbool.h>

#define PMGR_CALIB_EE_BASE   464u
#define PMGR_CALIB_SIZE       16u
#define PMGR_CALIB_MAGIC   0xCA1Bu
#define PMGR_CALIB_VERSION     1u

/* The nominal line, raw = 100*V, in the same fixed-point form as a record's.
 * ⛔ This is what an UNCALIBRATED unit uses, and it is NOT this pairing's fit -
 * see firmware/include/pmgr_adc.h, which remains the authoritative nominal. */
#define PMGR_CALIB_A_NOMINAL  25600u        /* 100.00 * 256 */

/* ⛔ VALIDITY BOUNDS (2.349). The last two are the SWEEP's OWN pre-registered
 * acceptance criteria - 2.338's 2-count residual and 2.339's 0.80 V span - re-
 * checked here, so ⭐ a record that would not have passed the sweep cannot
 * enable fast charge. */
#define PMGR_CALIB_A_MIN      23040u        /* nominal -10 % */
#define PMGR_CALIB_A_MAX      28160u        /* nominal +10 % */
#define PMGR_CALIB_B_ABS_MAX  25600         /* |b| <= 100 counts */
#define PMGR_CALIB_RESID_MAX     32u        /* <= 2.0 counts, in 1/16ths */
#define PMGR_CALIB_SPAN_MIN     800u        /* >= 0.80 V, in mV */

typedef struct {
    uint16_t magic;
    uint8_t  version;
    uint8_t  pairing_id;     /* installer-assigned. ⚠️ A HYBRID SWAP invalidates
                              * the record and the board CANNOT DETECT ONE (§6);
                              * this is a procedural handle, not a detector. */
    uint16_t a_q8;           /* slope     * 256 */
    int16_t  b_q8;           /* intercept * 256 */
    uint8_t  resid_q4;       /* max |residual| * 16, counts */
    uint16_t span_mv;        /* sweep span, mV */
    uint16_t rail_mv;        /* ⭐ the rail DURING the sweep. §6: the A/D is
                              * ratiometric to it and the record cannot know if
                              * it later moves. Recorded so a change is checkable. */
    uint8_t  reserved;
    uint16_t crc;            /* CRC-16/CCITT reflected over the 14 bytes before it */
} pmgr_calib_t;

/* Load and validate from EEPROM. False -> the gate is engaged. */
bool pmgr_calib_load(void);

/* True when a VALID record is in force. False -> nominal line, uncalibrated
 * flag set, and ⛔ FAST CHARGE DISABLED. */
bool pmgr_calib_valid(void);

/* ⭐ Convert a NOMINAL raw threshold into this unit's raw scale. With no valid
 * record this is the identity, so every caller can use it unconditionally. */
uint16_t pmgr_calib_threshold(uint16_t raw_nominal);

/* ⭐ The inverse, for the level REPORTED TO THE MAC (§5: the battery indicator
 * must agree with what charging decides). Identity with no valid record. */
uint8_t pmgr_calib_to_nominal(uint8_t level_measured);

/* ⭐ §5's VISIBLE "uncalibrated" FLAG, exposed through the $E8 memory view at
 * PMGR_CALIB_STATUS_ADDR. §5 requires the gate to be VISIBLE, not merely
 * effective - an uncalibrated board that looks identical to a calibrated one is
 * a support problem waiting to happen, and after 2.352 a routine reflash
 * produces exactly that board.
 *
 * ⛔ DELIBERATELY NOT IN THE $69 PowerFlags BYTE. Bit 0x80 there is free, but
 * that byte GOES TO THE MAC and the behaviour of its driver on an unknown bit
 * is not established. This region is OURS: it sits above the M50753's map, so
 * it invents nothing at an authentic address. */
#define PMGR_CALIB_STATUS_ADDR   0x0300u
#define PMGR_CALIB_PAIRING_ADDR  0x0301u
#define PMGR_CALIB_ST_UNCAL      0x01u   /* no valid record is in force */
#define PMGR_CALIB_ST_GATED      0x02u   /* ⭐ LATCHED: the gate has forced state 3
                                          * at least once this session - direct
                                          * evidence the gate RAN, which B3 needs */
uint8_t pmgr_calib_status(void);
uint8_t pmgr_calib_pairing(void);
void    pmgr_calib_note_gated(void);     /* called by the charging path */

/* Serialisation, exposed for the native tests and the installer tool. */
uint16_t pmgr_calib_crc(const uint8_t *b, uint8_t n);
void     pmgr_calib_encode(const pmgr_calib_t *c, uint8_t *out16);
bool     pmgr_calib_decode(const uint8_t *in16, pmgr_calib_t *c);

#endif
