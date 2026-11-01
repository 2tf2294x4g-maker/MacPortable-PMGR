#ifndef _AVR_SLEEP_H_
#define _AVR_SLEEP_H_
static inline void sleep_enable(void){}
static inline void sleep_disable(void){}
#ifdef MOCK_SLEEP_HOOK
void mock_sleep_cpu(void);          /* test_park.c: advances simulated time to the next wake event */
static inline void sleep_cpu(void){ mock_sleep_cpu(); }
#else
static inline void sleep_cpu(void){}
#endif
static inline void set_sleep_mode(int m){(void)m;}
#define SLEEP_MODE_PWR_DOWN 0
#define SLEEP_MODE_IDLE 1
#endif
