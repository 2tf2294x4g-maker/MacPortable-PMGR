#include "pmgr_pram.h"
#include <string.h>

/* ⭐⭐ PRAM PERSISTENCE - NON-BLOCKING, TWO-SLOT. MEASUREMENTS 2.155 / 2.156.
 *
 * ⛔ WHY: the old pmgr_pram_commit() wrote EEPROM SYNCHRONOUSLY from inside the
 * $31 / xPRAM / $E8 handlers. One erase+write is ~10 ms (2.157), so a Control
 * Panel change blocked the bus for ~31 ms. The host's requests arriving in that
 * window were abandoned after ~500 us each, and the machine froze (2.152, 2.155,
 * scope + validated onset log).
 *
 * NOW:
 *   - RAM (s_pram, s_cksum) is authoritative. Handlers update it and return at
 *     once; reads are served from it. No handler touches EEPROM.
 *   - pmgr_pram_service(), from the main loop, advances a save by AT MOST ONE
 *     byte erase+write per call and NEVER waits for EEBUSY. The core keeps
 *     executing during the write (G0, 2.157), so the bus is served throughout.
 *
 * LAYOUT - two slots of 132 bytes in the 512-byte EEPROM:
 *
 *     +0        seq      generation number; 0xFF = invalid
 *     +1..128   pram     s_pram[0..127]
 *     +129      cksum    s_cksum ($AF)
 *     +130..131 crc      CRC-16 over seq, pram, cksum (little-endian)
 *
 * A slot is VALID when seq != 0xFF and the CRC matches. Boot loads the newer of
 * the valid slots (seq distance 1..127 mod 256); neither valid -> blank PRAM,
 * which the Mac initialises exactly as before.
 *
 * INTERRUPTED SAVES. Only the INACTIVE slot is ever written, so the last durable
 * generation is never touched. Order of one save:
 *
 *     1  seq <- 0xFF                    invalidate FIRST
 *     2  pram + cksum bytes that differ from the snapshot
 *     3  crc  - computed over the NEW seq, so it cannot match while seq is 0xFF
 *     4  seq <- new seq                 LAST - the commit point
 *
 * A reset or power loss before step 4 completes leaves the old slot the only
 * valid one. A byte torn by brown-out fails the CRC (seq is covered too, so a
 * torn invalidate or commit byte cannot make a stale or partial slot valid,
 * short of a 1-in-65536 CRC collision).
 *
 * CONCURRENT CHANGES. A save copies RAM into s_snap at its start and writes
 * only the snapshot. A host write during the save sets s_dirty again, and a new
 * save starts when this one completes: changes can be superseded, never lost.
 * (Every writer calls pmgr_pram_commit() in the same handler call, and service
 * runs only from the main loop, so a snapshot never sees data without its $AF.)
 *
 * DURABILITY. A change is durable when step 4's write completes AND the slot
 * reads back intact. pmgr_pram_save_pending() is true until then.
 * pmgr_pram_flush() completes it synchronously - only where the host is held
 * (sleep / Shut Down entry, PMGR_RESET* restart). Power loss loses a change that
 * is not yet durable (worst case a few writes, ~10 ms each) and the previous
 * generation loads. Documented divergence: the original's PRAM is battery RAM.
 *
 * ⛔ Every flash erases EEPROM (EESAVE clear). There is deliberately no
 * migration from the old single-image layout (magic 0xA7 at +0). */

#define SLOT_SIZE    132u
#define SLOT_BASE(n) ((uint16_t)((n) * SLOT_SIZE))
#define OFF_SEQ      0u
#define OFF_DATA     1u             /* pram[128] then cksum */
#define DATA_LEN     (PRAM_SIZE + 1u)
#define OFF_CRC      (OFF_DATA + DATA_LEN)
#define SEQ_INVALID  0xFFu

/* ⭐ FAILURE POLICY (review, MEASUREMENTS 2.184). Every loop here is bounded.
 *
 *   read-back failures  a save whose slot does not read back is retried, at most
 *                       PRAM_SAVE_MAX_ATTEMPTS times in a row; then the saver
 *                       STOPS (s_save_failed latched, nothing pending). The last
 *                       valid slot is untouched and still loads; RAM keeps the
 *                       settings until power is lost. The next change from the
 *                       host starts a fresh series of attempts.
 *   flush               polls for at most PRAM_FLUSH_MAX_POLLS x PRAM_FLUSH_POLL_US
 *                       (5 s; a full 132-byte save is ~1.35 s, two back to back
 *                       ~2.7 s) and returns false if not durable - shutdown and
 *                       restart then proceed.
 *   init                waits for a write left in progress for at most
 *                       PRAM_INIT_BUSY_POLLS x PRAM_FLUSH_POLL_US (100 ms; one
 *                       erase+write is ~10 ms); if the NVM controller is still busy
 *                       the slots are not trusted and PRAM starts blank.
 *
 * Previously none of these was bounded: a persistently failing EEPROM byte
 * rewrote the slot forever (wear, never idle), flush() never returned, and a
 * stuck EEBUSY hung boot. */
#define PRAM_SAVE_MAX_ATTEMPTS  3u
#define PRAM_FLUSH_POLL_US      100u
#define PRAM_FLUSH_MAX_POLLS    50000UL     /* 5 s */
#define PRAM_INIT_BUSY_POLLS    1000u       /* 100 ms */

/* ---- EEPROM primitives --------------------------------------------------- */
#ifdef __AVR__
#include <avr/io.h>
#include <avr/cpufunc.h>
#include <util/delay.h>
#include "pmgr_pins.h"
#define PRAM_POLL_DELAY()  _delay_us(PRAM_FLUSH_POLL_US)

static inline bool    ee_busy(void)         { return (NVMCTRL.STATUS & NVMCTRL_EEBUSY_bm) != 0u; }
static inline uint8_t ee_get(uint16_t a)    { return *(uint8_t *)(MAPPED_EEPROM_START + a); }

/* Starts ONE erase+write and returns - never waits. Call only when !ee_busy(). */
static void ee_start_write(uint16_t a, uint8_t v)
{
    _PROTECTED_WRITE_SPM(NVMCTRL.CTRLA, NVMCTRL_CMD_EEERWR_gc);
    *(uint8_t *)(MAPPED_EEPROM_START + a) = v;
}

/* Clears the NVM command once the write has finished. */
static void ee_end_write(void)
{
    _PROTECTED_WRITE_SPM(NVMCTRL.CTRLA, NVMCTRL_CMD_NONE_gc);
}
#else
#include "pmgr_ee_hal.h"            /* native tests: a simulated EEPROM */
#define PRAM_POLL_DELAY()  ((void)0)     /* the simulator counts polls instead */
#endif

static uint16_t crc_update(uint16_t crc, uint8_t d)   /* CRC-16/CCITT, reflected */
{
    d ^= (uint8_t)crc;
    d ^= (uint8_t)(d << 4);
    return (uint16_t)((((uint16_t)d << 8) | (crc >> 8)) ^ (uint8_t)(d >> 4) ^ ((uint16_t)d << 3));
}

static uint16_t image_crc(uint8_t seq, const uint8_t *data)
{
    uint16_t crc = crc_update(0xFFFFu, seq);
    for (uint8_t i = 0; i < DATA_LEN; i++) crc = crc_update(crc, data[i]);
    return crc;
}

static bool slot_valid(uint8_t n, uint8_t *seq_out)
{
    const uint16_t b = SLOT_BASE(n);
    uint8_t data[DATA_LEN];
    const uint8_t seq = ee_get(b + OFF_SEQ);
    if (seq == SEQ_INVALID) return false;
    for (uint8_t i = 0; i < DATA_LEN; i++) data[i] = ee_get((uint16_t)(b + OFF_DATA + i));
    const uint16_t crc = (uint16_t)(ee_get(b + OFF_CRC) | ((uint16_t)ee_get(b + OFF_CRC + 1u) << 8));
    if (crc != image_crc(seq, data)) return false;
    *seq_out = seq;
    return true;
}

static uint8_t next_seq(uint8_t s) { s++; return (s == SEQ_INVALID) ? 0u : s; }

static uint8_t s_pram[PRAM_SIZE];
static uint8_t s_cksum;
static bool s_dirty;                /* RAM differs from the newest durable slot */

/* ---- the saver ----------------------------------------------------------- */
typedef enum { SAVE_IDLE, SAVE_CRCCALC, SAVE_INVALIDATE, SAVE_DATA, SAVE_CRC, SAVE_SEQ, SAVE_VERIFY } save_step_t;

/* ⭐ WORK BUDGET PER CALL. Not only EEPROM writes stall the bus: computing a
 * 130-byte CRC or reading back a whole slot in one call is a few hundred us of
 * main loop in which PMREQ* is not looked at. Every per-byte step (CRC byte,
 * EEPROM compare, verify read) costs one unit, and the call returns when the
 * budget is spent. test_save.c enforces the bound on EEPROM reads per call. */
#define SAVE_BUDGET  16u

static save_step_t s_step;
static bool     s_writing;          /* an erase+write was started; end it when !busy */
static uint8_t  s_active;           /* slot holding the newest durable generation */
static bool     s_have_active;
static uint8_t  s_active_seq;
static uint8_t  s_target;           /* slot being written */
static uint8_t  s_new_seq;
static uint8_t  s_idx;
static uint16_t s_crc;
static uint8_t  s_snap[DATA_LEN];
static uint16_t s_save_count;       /* completed saves since init - diagnostics/tests */
static uint8_t  s_save_attempts;    /* consecutive read-back failures in this series */
static bool     s_retrying;         /* s_dirty was set by a failed read-back, not by the host */
static bool     s_save_failed;      /* latched: the saver gave up (policy above); cleared by a good save */

/* TIME LIVES IN .noinit - it must SURVIVE A RESET.
 *
 * In the original the time is plain RAM in a chip that never loses power (J21
 * pins 1/2 are VCC ALWAYS ON), so a reset does not disturb it. That is why
 * pmgr.asm validates rather than reinitialises at startup:
 *
 *      157  jsr CombineTime
 *      158  cmp Time            ; does RAM still agree with its checksum?
 *      159  beq LAB_E89A        ; yes - KEEP the time across the reset
 *      160  ldx #9 / sta $24,X  ; no  - zero it
 *
 * and refreshes the checksum on every 1 Hz tick (:594-595).
 *
 * An ordinary static would be .bss, which the C runtime zeroes on EVERY
 * startup - so any reset would silently forget the date where the original
 * kept it. .noinit is excluded from that clearing, giving us the same
 * "RAM persists, trust it only if the checksum agrees" model.
 *
 * ⚠️ NOT a substitute for a battery. On real power loss .noinit is garbage,
 * exactly as the original's RAM would be - and the checksum is what catches
 * that. Retaining time across POWER LOSS would need an RTC or a supercap;
 * it is not something the original did.
 *
 * ⚠️ And NOT EEPROM. Writing the time every second would exhaust a 100k-cycle
 * cell in about 28 hours. PRAM is EEPROM-backed because it changes rarely;
 * the time deliberately is not. */
/* .noinit is an AVR/ELF linker section. The native test build is Mach-O and
 * rejects the name, so it falls back to ordinary statics - correct there,
 * because the host tests exercise the checksum LOGIC, not reset survival,
 * which cannot be tested off-target anyway. */
#ifdef __AVR__
#  define PMGR_NOINIT __attribute__((section(".noinit")))
#else
#  define PMGR_NOINIT
#endif

static uint32_t s_time     PMGR_NOINIT;
static uint8_t  s_time_sum PMGR_NOINIT;

void pmgr_pram_init(void)
{
    /* A save interrupted by a software reset may still have its last byte in
     * progress; reading the slots mid-write would be meaningless. Bounded
     * (failure policy): a controller still busy after 100 ms is not trusted. */
    uint16_t polls = 0;
    while (ee_busy() && polls < PRAM_INIT_BUSY_POLLS) { PRAM_POLL_DELAY(); polls++; }
    const bool nvm_stuck = ee_busy();
    if (!nvm_stuck) ee_end_write();
#if defined(__AVR__) && defined(PMGR_SAVE_MARKER)
    DBG1_PORT.OUTCLR = DBG1_bm;
    DBG1_PORT.DIRSET = DBG1_bm;
#endif

    uint8_t seq0 = 0, seq1 = 0;
    const bool v0 = !nvm_stuck && slot_valid(0, &seq0);
    const bool v1 = !nvm_stuck && slot_valid(1, &seq1);

    s_have_active = v0 || v1;
    if (v0 && v1) {
        const uint8_t d = (uint8_t)(seq1 - seq0);
        s_active = (d >= 1u && d <= 127u) ? 1u : 0u;
    } else {
        s_active = v1 ? 1u : 0u;
    }

    if (s_have_active) {
        const uint16_t b = SLOT_BASE(s_active);
        s_active_seq = s_active ? seq1 : seq0;
        for (uint8_t i = 0; i < PRAM_SIZE; i++) s_pram[i] = ee_get((uint16_t)(b + OFF_DATA + i));
        s_cksum = ee_get((uint16_t)(b + OFF_DATA + PRAM_SIZE));
    } else {
        /* Never written (or both slots damaged). Leave PRAM zeroed -
         * pmgr_pram_checksum() will report it invalid via the all-zero rule,
         * exactly as the original does for blank PRAM, and the Mac will
         * initialise it. */
        memset(s_pram, 0, PRAM_SIZE);
        s_cksum = 0;
        s_active = 1u;              /* so the first save targets slot 0 */
        s_active_seq = SEQ_INVALID; /* next_seq(0xFF) == 0 */
    }

    /* pmgr.asm 157-165. Validate the time that survived in .noinit rather than
     * reinitialising it. A reset keeps the clock; a cold power-up does not,
     * because .noinit will be garbage and the checksum will not agree. */
    if (pmgr_time_combine() != s_time_sum) {
        s_time     = 0;                 /* 160-165  sta $24,X */
        s_time_sum = pmgr_time_combine();
    }

    s_dirty   = false;
    s_step    = SAVE_IDLE;
    s_writing = false;
    s_save_count = 0;
    s_save_attempts = 0;
    s_retrying = false;
    s_save_failed = nvm_stuck;
}

/* pmgr.asm 141-145 InitRAM: zero all 128 bytes and recompute the checksum
 * into $AF. Called when FUN_EF84 reports the stored checksum bad.
 *
 * NOTE the original also restores lowBatteryLevel/deadBatteryLevel/HICHGLevel
 * ($71/$72/$73) immediately after, because those live INSIDE the zeroed
 * $2F..$AE block and the wipe destroys them. Ours are compile-time constants
 * in pmgr_adc.h, so there is nothing to restore - see the note in
 * pmgr_entry_state(). */
void pmgr_pram_zero(void)
{
    memset(s_pram, 0, PRAM_SIZE);
    s_cksum = 0;
    s_dirty = true;                 /* saved so the repair survives */
    pmgr_pram_commit();             /* jsr FUN_EF84 / sta $AF (:147-148) */
}

uint8_t pmgr_pram_read(uint8_t idx)
{
    return (idx < PRAM_SIZE) ? s_pram[idx] : 0u;
}

void pmgr_pram_write(uint8_t idx, uint8_t v)
{
    /* The original writes through ($1B),Y with no bounds check - a bad length
     * would walk into $AF and beyond. We clamp; corrupting our own checksum
     * and time is not a faithfulness worth having. */
    if (idx < PRAM_SIZE && s_pram[idx] != v) {
        s_pram[idx] = v;
        s_dirty = true;
    }
}

uint8_t pmgr_pram_checksum(bool *valid)
{
    uint8_t sum = 0;
    uint8_t any = 0;
    for (uint8_t i = 0; i < PRAM_SIZE; i++) {   /* ldx #0 / adc $2F,X / inx / bpl */
        sum += s_pram[i];
        any |= s_pram[i];                       /* the ora $2F,X pass */
    }
    if (valid) {
        /* carry SET (invalid) when the stored checksum disagrees, or when
         * every byte is zero - .LAB_EF9C falls through to .LAB_EFA3 */
        *valid = (sum == s_cksum) && (any != 0u);
    }
    return sum;
}

void pmgr_pram_commit(void)
{
    /* jsr FUN_EF84 / sta $AF - the freshly computed sum becomes the checksum.
     * ⭐ Returns at once. Persistence happens in pmgr_pram_service(). */
    s_cksum = pmgr_pram_checksum(NULL);
}

bool pmgr_pram_save_pending(void)
{
    return s_dirty || s_step != SAVE_IDLE || s_writing;
}

uint16_t pmgr_pram_save_count(void) { return s_save_count; }

/* Starts the next write if the byte differs; returns true if a write started. */
static bool write_if_differs(uint16_t a, uint8_t v)
{
    if (ee_get(a) == v) return false;
    ee_start_write(a, v);
    s_writing = true;
    return true;
}

void pmgr_pram_service(void)
{
    if (ee_busy()) return;
    if (s_writing) { ee_end_write(); s_writing = false; }

    uint8_t budget = SAVE_BUDGET;
    const uint16_t base = SLOT_BASE(s_target);

    for (;;) {
        switch (s_step) {
        case SAVE_IDLE:
            if (!s_dirty) goto out;
            if (!s_retrying) s_save_attempts = 0;       /* a fresh change: a fresh series */
            s_retrying = false;
            memcpy(s_snap, s_pram, PRAM_SIZE);
            s_snap[PRAM_SIZE] = s_cksum;
            s_dirty   = false;
            s_target  = (uint8_t)(s_active ^ 1u);
            s_new_seq = next_seq(s_have_active ? s_active_seq : SEQ_INVALID);
            s_crc     = crc_update(0xFFFFu, s_new_seq);
            s_idx     = 0;
            s_step    = SAVE_CRCCALC;
            goto out;               /* base is stale for the new target */

        case SAVE_CRCCALC:
            while (s_idx < DATA_LEN) {
                if (budget-- == 0u) goto out;
                s_crc = crc_update(s_crc, s_snap[s_idx++]);
            }
            s_step = SAVE_INVALIDATE;
            break;

        case SAVE_INVALIDATE:
            s_step = SAVE_DATA;
            s_idx  = 0;
            if (write_if_differs(base + OFF_SEQ, SEQ_INVALID)) goto out;
            break;

        case SAVE_DATA:
            while (s_idx < DATA_LEN) {
                if (budget-- == 0u) goto out;
                const uint8_t i = s_idx++;
                if (write_if_differs((uint16_t)(base + OFF_DATA + i), s_snap[i])) goto out;
            }
            s_step = SAVE_CRC;
            s_idx  = 0;
            break;

        case SAVE_CRC:
            while (s_idx < 2u) {
                const uint8_t i = s_idx++;
                if (write_if_differs((uint16_t)(base + OFF_CRC + i), (uint8_t)(s_crc >> (8u * i)))) goto out;
            }
            s_step = SAVE_SEQ;
            break;

        case SAVE_SEQ:              /* the commit point */
            s_step = SAVE_VERIFY;
            s_idx  = 0;
            if (write_if_differs(base + OFF_SEQ, s_new_seq)) goto out;
            break;

        case SAVE_VERIFY:
            /* Read the slot back: data equal to the snapshot, CRC and seq equal
             * to what was computed - together, exactly "slot valid with this
             * content". Chunked like the rest. */
            while (s_idx < DATA_LEN) {
                if (budget-- == 0u) goto out;
                const uint8_t i = s_idx++;
                if (ee_get((uint16_t)(base + OFF_DATA + i)) != s_snap[i]) { s_idx = 0xFFu; break; }
            }
            if (s_idx != 0xFFu &&
                ee_get(base + OFF_CRC) == (uint8_t)s_crc &&
                ee_get(base + OFF_CRC + 1u) == (uint8_t)(s_crc >> 8) &&
                ee_get(base + OFF_SEQ) == s_new_seq) {
                s_active      = s_target;
                s_active_seq  = s_new_seq;
                s_have_active = true;
                s_save_count++;
                s_save_attempts = 0;
                s_save_failed = false;
            } else if (++s_save_attempts < PRAM_SAVE_MAX_ATTEMPTS) {
                /* A write did not stick. The target is not accepted as active;
                 * save again (same target, since s_active is unchanged) - unless
                 * the host has changed PRAM meanwhile, which the retry also covers. */
                s_dirty = true;
                s_retrying = true;
            } else {
                /* ⛔ Give up (failure policy). The previous valid slot still loads;
                 * a host write made during this failing save keeps s_dirty and
                 * starts a new series on its own. */
                s_save_failed = true;
            }
            s_step = SAVE_IDLE;
            goto out;
        }
    }
out:
#if defined(__AVR__) && defined(PMGR_SAVE_MARKER)
    /* A/B builds only (2.156): DBG1 high while a save is outstanding. */
    if (pmgr_pram_save_pending()) DBG1_PORT.OUTSET = DBG1_bm; else DBG1_PORT.OUTCLR = DBG1_bm;
#endif
    return;
}

bool pmgr_pram_save_failed(void) { return s_save_failed; }

bool pmgr_pram_flush(void)
{
    for (uint32_t polls = 0; pmgr_pram_save_pending(); polls++) {
        if (polls >= PRAM_FLUSH_MAX_POLLS) {
            /* ⛔ Not durable in 5 s (failure policy): abandon the save. The last
             * valid slot is untouched; RAM keeps the settings until power loss. */
            if (s_writing && !ee_busy()) ee_end_write();
            s_writing = false;
            s_step    = SAVE_IDLE;
            s_dirty   = false;
            s_retrying = false;
            s_save_failed = true;
            return false;
        }
        pmgr_pram_service();
        PRAM_POLL_DELAY();
    }
    return !s_save_failed;
}

/* ---- time ---------------------------------------------------------------- */

uint32_t pmgr_time_get(void)        { return s_time; }

/* Every write to the time refreshes its checksum, matching the original:
 *   :594-595  the 1 Hz tick   - jsr CombineTime / sta Time
 *   :1050-51  timeWrite       - jsr CombineTime / sta Time
 * Without this the next reset would find a stale checksum and zero the clock. */
void pmgr_time_set(uint32_t t)
{
    s_time     = t;
    s_time_sum = pmgr_time_combine();
}

void pmgr_time_tick(void)                           /* inc timeA, carry upward */
{
    s_time++;
    s_time_sum = pmgr_time_combine();               /* :594-595 */
}

uint8_t pmgr_time_combine(void)
{
    /* CombineTime: 0 + timeD + timeC + timeB + timeA, 8-bit wrap */
    return (uint8_t)((s_time >> 24) + (s_time >> 16) + (s_time >> 8) + s_time);
}
