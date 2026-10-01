/* G1 (MEASUREMENTS 2.180): the command-byte latch for serving the host during ADB work.
 * pmgr_bus.c + pmgr_proto.c compiled natively with PMGR_ADB_HOST_SERVICE; the Mac's side of the
 * handshake is modelled inside _delay_us(), one simulated microsecond at a time. */
#include "pmgr_bus.h"
#include "pmgr_proto.h"
#include "pmgr_handlers.h"
#include "pmgr_pins.h"
#include <stdio.h>
#include <string.h>

PORT_t PORTA, PORTB, PORTC, PORTD, PORTE, PORTF, PORTG;
static int fails;
#define CHECK(c, m) do { if (c) printf("  PASS  %s\n", m); else { printf("  FAIL  %s\n", m); fails++; } } while (0)

/* ---- dispatch stubs: record what was dispatched ---- */
static pmgr_packet_t last; static int dispatched;
static void rec(pmgr_packet_t *p) { last = *p; dispatched++; }
void pmgr_cmd_invalid(pmgr_packet_t *p) { rec(p); }   void pmgr_cmd_power(pmgr_packet_t *p) { rec(p); }
void pmgr_cmd_adb(pmgr_packet_t *p) { rec(p); }       void pmgr_cmd_time_pram(pmgr_packet_t *p) { rec(p); }
void pmgr_cmd_contrast(pmgr_packet_t *p) { rec(p); }  void pmgr_cmd_modem(pmgr_packet_t *p) { rec(p); }
void pmgr_cmd_battery(pmgr_packet_t *p) { rec(p); }   void pmgr_cmd_sleep(pmgr_packet_t *p) { rec(p); }
void pmgr_cmd_timer(pmgr_packet_t *p) { rec(p); }     void pmgr_cmd_sound(pmgr_packet_t *p) { rec(p); }
void pmgr_cmd_pmgr(pmgr_packet_t *p) { rec(p); }      void pmgr_cmd_read_int(pmgr_packet_t *p) { rec(p); }
void pmgr_cmd_read_adb(pmgr_packet_t *p) { rec(p); }

/* ---- host model ---- */
enum { H_IDLE, H_WAIT_ACK, H_RELEASE, H_WAIT_UNACK, H_GAP, H_STUCK };
static int h_state = H_IDLE, h_i, h_n, h_timer, h_ackwait;
static uint8_t h_bytes[40];
static int pmack_low;
static long now_us, acks_seen;
static int h_sent_ok;          /* bytes whose handshake the host completed */
static int h_release_us = 10;  /* host's PMREQ* release delay after seeing PMACK* */

static void req(int low) { if (low) PORTB.IN &= (uint8_t)~PMREQ_N_bm; else PORTB.IN |= PMREQ_N_bm; }
static void host_start(const uint8_t *b, int n) { memcpy(h_bytes, b, n); h_n = n; h_i = 0; h_sent_ok = 0;
    PORTA.IN = b[0]; req(1); h_state = H_WAIT_ACK; h_timer = 0; h_ackwait = 500; }
static void host_step(void)
{
    if (PORTB.OUTCLR & PMACK_N_bm) { pmack_low = 1; PORTB.OUTCLR &= (uint8_t)~PMACK_N_bm; acks_seen++; }
    if (PORTB.OUTSET & PMACK_N_bm) { pmack_low = 0; PORTB.OUTSET &= (uint8_t)~PMACK_N_bm; }
    switch (h_state) {
    case H_WAIT_ACK:
        if (pmack_low) { h_state = H_RELEASE; h_timer = h_release_us; }
        else if (++h_timer > h_ackwait) { req(0); h_state = H_IDLE; }       /* host gives up (not retried here) */
        break;
    case H_RELEASE:
        if (--h_timer == 0) { req(0); h_state = H_WAIT_UNACK; h_timer = 0; }
        break;
    case H_WAIT_UNACK:
        if (!pmack_low) { h_sent_ok++; h_i++; h_state = (h_i < h_n) ? H_GAP : H_IDLE; h_timer = 40; }
        else if (++h_timer > 500) h_state = H_IDLE;
        break;
    case H_GAP:
        if (--h_timer == 0) { PORTA.IN = h_bytes[h_i]; req(1); h_state = H_WAIT_ACK; h_timer = 0; h_ackwait = 8000; }
        break;
    default: break;
    }
}
void _delay_us(double us) { for (long i = 0; i < (long)us; i++) { now_us++; host_step(); } }
void _delay_ms(double ms) { _delay_us(ms * 1000.0); }

static void reset_host(void) { h_state = H_IDLE; req(0); pmack_low = 0; PORTB.OUTCLR = PORTB.OUTSET = 0; dispatched = 0; }

int main(void)
{
    const uint8_t cmd31[] = { 0x31, 20, 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20 };

    /* 1. baseline path: no latch involved */
    reset_host(); host_start(cmd31, sizeof cmd31);
    pmgr_poll();
    CHECK(dispatched == 1 && last.command == 0x31 && last.length == 20 && last.data[19] == 20,
          "normal command without a latch: packet exact, dispatched once");

    /* 2. command byte latched during ADB work, count/data after */
    reset_host(); host_start(cmd31, sizeof cmd31);
    pmgr_bus_latch_arm(true);
    pmgr_latch_t r = pmgr_bus_latch_command();
    CHECK(r == LATCH_OK && pmgr_bus_has_latched(), "request during ADB work: command byte latched (fast release)");
    CHECK(pmgr_bus_latch_command() == LATCH_NONE, "second call while latched: nothing done");
    _delay_us(3000);                                  /* the rest of the ADB transaction */
    CHECK(h_state == H_WAIT_ACK && h_i == 1, "host moved on to the count byte and is waiting (long timeout)");
    pmgr_bus_latch_arm(false);
    pmgr_poll();
    CHECK(dispatched == 1 && last.command == 0x31 && last.length == 20 && memcmp(last.data, cmd31 + 2, 20) == 0,
          "after ADB: full packet received and dispatched once, command taken from the latch");
    _delay_us(1);   /* the model sees the final PMACK* release on the next simulated microsecond */
    CHECK(!pmgr_bus_has_latched() && h_sent_ok == (int)sizeof cmd31, "latch consumed; host completed every byte");

    /* 3. disarmed: never latches */
    reset_host(); host_start(cmd31, sizeof cmd31);
    CHECK(pmgr_bus_latch_command() == LATCH_NONE && !pmgr_bus_has_latched(), "disarmed: request left for pmgr_poll()");
    pmgr_poll();
    CHECK(dispatched == 1 && last.command == 0x31, "  ...and pmgr_poll() still takes it normally");

    /* 4. no request: nothing */
    reset_host();
    pmgr_bus_latch_arm(true);
    CHECK(pmgr_bus_latch_command() == LATCH_NONE, "armed, PMREQ* high: nothing done");

    /* 5. host never releases PMREQ* */
    reset_host(); PORTA.IN = 0x10; req(1); h_state = H_STUCK;
    long t0 = now_us;
    r = pmgr_bus_latch_command();
    CHECK(r == LATCH_FAILED && !pmgr_bus_has_latched(), "host never releases: latch fails, nothing stored");
    long t_done = now_us;
    _delay_us(1);   /* as above */
    CHECK(!pmack_low && (t_done - t0) <= (long)PMGR_REQ_TIMEOUT_US + 5, "  ...PMACK* released after the receive timeout");

    /* 6. slow release (> PMGR_LATCH_FAST_US) is reported, byte still latched and usable */
    reset_host(); h_release_us = 100; host_start(cmd31, sizeof cmd31);
    pmgr_bus_latch_arm(true);
    r = pmgr_bus_latch_command();
    CHECK(r == LATCH_SLOW && pmgr_bus_has_latched(), "slow host release: LATCH_SLOW reported, byte latched");
    pmgr_bus_latch_arm(false);
    h_release_us = 10;
    pmgr_poll();
    CHECK(dispatched == 1 && last.command == 0x31 && last.data[0] == 1, "  ...and the packet still completes");

    /* 7. every command group: latched command byte dispatches to the same place as an unlatched one */
    {
        int ok = 1;
        for (int g = 0; g < 16; g++) {
            uint8_t c[] = { (uint8_t)((g << 4) | 0x0), 1, 0x5A };
            reset_host(); host_start(c, 3); pmgr_poll();
            pmgr_packet_t a = last; int da = dispatched;
            reset_host(); host_start(c, 3); pmgr_bus_latch_arm(true); pmgr_bus_latch_command(); pmgr_bus_latch_arm(false);
            _delay_us(1000); pmgr_poll();
            if (!(da == 1 && dispatched == 1 && a.command == last.command && a.length == last.length && a.data[0] == last.data[0])) ok = 0;
        }
        CHECK(ok, "all 16 command groups: latched and unlatched receive dispatch identically");
    }

    printf("\n  latch counters: ok %u slow %u failed %u\n", pmgr_latch_ok, pmgr_latch_slow, pmgr_latch_failed);
    printf("  %s\n", fails ? "FAILURES" : "all pass");
    return fails != 0;
}
