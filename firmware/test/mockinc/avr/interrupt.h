#ifndef MOCK_INTERRUPT_H
#define MOCK_INTERRUPT_H
#define ISR(v) void v##_handler(void)
static inline void sei(void) {}
static inline void cli(void) {}
#endif
