#include "dmod.h"
#include "dmosi.h"
#include "registers.h"
#include "clock_wait.h"
#include <errno.h>
#include <stddef.h>

/* Only our selected RCC ready source is owned, never the entire RCC IRQ.
 * DMOD dispatches a shared IRQ to each registered module handler. */
static dmosi_semaphore_t volatile clock_event;
static volatile uint32_t owned_flag;
struct irq_guard { bool enabled; uint8_t priority; uint32_t flag; };

int stm32_rtc_wait_register(volatile uint32_t* reg, uint32_t mask, uint32_t value)
{
    if (!dmosi_is_started() || Dmod_IsInsideInterrupt()) return -ENOTSUP;
    uint64_t start = Dmod_GetUptime();
    for (;;) {
        if ((*reg & mask) == value) return 0;
        if (Dmod_GetUptime() - start >= REGISTER_TIMEOUT_MS) return -ETIMEDOUT;
        dmosi_thread_sleep(1); /* Block this task so lower-priority work can run. */
    }
}

static int arm_irq(struct irq_guard* guard, dmosi_semaphore_t event)
{
    uint32_t minimum = dmosi_get_min_interrupt_priority();
    Dmod_EnterCritical();
    guard->enabled = (NVIC32(0) & RCC_IRQ_BIT) != 0;
    guard->priority = NVIC_PRIORITY;
    if (clock_event != NULL || (RCC(RCC_CIR) & (guard->flag << 8)) ||
        (guard->enabled && guard->priority < minimum)) {
        Dmod_ExitCritical(); return -EBUSY;
    }
    clock_event = event;
    owned_flag = guard->flag;
    if (!guard->enabled) NVIC_PRIORITY = (uint8_t)minimum;
    /* Preserve other enables, clear only our stale flag, then arm our source. */
    RCC(RCC_CIR) = (RCC(RCC_CIR) & RCC_IRQ_ENABLES) |
        (guard->flag << 8) | (guard->flag << 16);
    NVIC32(0) = RCC_IRQ_BIT;
    Dmod_ExitCritical();
    return 0;
}
static void disarm_irq(const struct irq_guard* guard)
{
    Dmod_EnterCritical();
    uint32_t enables = RCC(RCC_CIR) & RCC_IRQ_ENABLES & ~(guard->flag << 8);
    RCC(RCC_CIR) = enables | (guard->flag << 16);
    owned_flag = 0;
    clock_event = NULL;
    if (!guard->enabled && enables == 0) {
        NVIC32(0x80) = RCC_IRQ_BIT;
        NVIC_PRIORITY = guard->priority;
    }
    Dmod_ExitCritical();
}
int stm32_rtc_start_clock(dmrtc_clock_t clock, uint32_t timeout_ms)
{
    if ((clock != DMRTC_CLOCK_LSE && clock != DMRTC_CLOCK_LSI) ||
        timeout_ms == 0 || timeout_ms > 10000) return -EINVAL;
    if (!dmosi_is_started() || Dmod_IsInsideInterrupt()) return -ENOTSUP;
    volatile uint32_t* reg = clock == DMRTC_CLOCK_LSE ? &RCC(BDCR) : &RCC(CSR);
    struct irq_guard guard = {false, 0, clock == DMRTC_CLOCK_LSE ? 2u : 1u};
    if ((*reg & 3u) == 3u) return 0; /* Oscillator already enabled and ready. */
    dmosi_semaphore_t event = dmosi_semaphore_create(0, 1);
    if (event == NULL) return -ENOMEM;
    int rc = arm_irq(&guard, event);
    if (rc == 0) {
        Dmod_EnterCritical(); *reg |= 1u; Dmod_ExitCritical();
        /* Covers a ready event before the wait; semaphore retains an early ISR post. */
        if (!(*reg & 2u)) {
            rc = dmosi_semaphore_wait(event, 1, (int32_t)timeout_ms);
            if (rc == 0) Dmod_Printf("dmrtc: RTC clock ready (RCC IRQ)\n");
        }
        if (rc == 0 && !(*reg & 2u)) rc = -EIO;
        disarm_irq(&guard); /* Disable source before destroying the ISR's semaphore. */
    }
    dmosi_semaphore_destroy(event);
    return rc;
}
void stm32_rtc_clock_irq(void)
{
    uint32_t cir = RCC(RCC_CIR);
    uint32_t pending = cir & (cir >> 8) & owned_flag;
    if (pending == 0) return;
    RCC(RCC_CIR) = (cir & RCC_IRQ_ENABLES & ~(pending << 8)) | (pending << 16);
    dmosi_semaphore_t event = clock_event;
    if (event != NULL) dmosi_semaphore_post(event, 1);
}
