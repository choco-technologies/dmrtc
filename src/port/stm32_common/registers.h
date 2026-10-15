#ifndef STM32_RTC_REGISTERS_H
#define STM32_RTC_REGISTERS_H
#include <stdint.h>
/* STM32F4 RM0090 and STM32F7 RM0385, RCC/PWR/RTC register maps. */
#define REG(base, off) (*(volatile uint32_t*)((uintptr_t)(base) + (off)))
#define RCC(off) REG(0x40023800u, off)
#define PWR REG(0x40007000u, 0)
#define RTC(off) REG(0x40002800u, off)
#define APB1ENR 0x40
#define BDCR 0x70
#define CSR 0x74
#define PWREN (1u << 28)
#define DBP (1u << 8)
#define LSEON 1u
#define LSERDY 2u
#define LSEBYP 4u
#define LSION 1u
#define LSIRDY 2u
#define RTCSEL (3u << 8)
#define RTCEN (1u << 15)
#define TR 0x00
#define DR 0x04
#define CR 0x08
#define ISR 0x0c
#define PRER 0x10
#define WPR 0x24
#define BKP(index) RTC(0x50 + 4u * (index))
#define FMT (1u << 6)
#define BYPSHAD (1u << 5)
#define INIT (1u << 7)
#define INITF (1u << 6)
#define INITS (1u << 4)
#define MARKER 0x52544331u
#endif
