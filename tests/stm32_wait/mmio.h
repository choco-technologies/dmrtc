#ifndef TEST_MMIO_H
#define TEST_MMIO_H
#include <stdint.h>
extern volatile uint32_t test_rcc[64], test_nvic[64], test_pwr, test_rtc[64];
extern volatile uint8_t test_priority;
volatile uint32_t* test_register(uintptr_t base, unsigned offset);
#define REG(base, off) (*test_register(base, off))
#define NVIC32(off) test_nvic[(off)/4]
#define NVIC_PRIORITY test_priority
#endif
