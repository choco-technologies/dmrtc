#ifndef STM32_RTC_CLOCK_WAIT_H
#define STM32_RTC_CLOCK_WAIT_H
#include "dmrtc_types.h"
int stm32_rtc_start_clock(dmrtc_clock_t clock, uint32_t timeout_ms);
void stm32_rtc_clock_irq(void);
/* DBP/INITF have no hardware interrupt; sleeps between samples. */
int stm32_rtc_wait_register(volatile uint32_t* reg, uint32_t mask, uint32_t value);
#endif
