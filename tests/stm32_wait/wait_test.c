#include "dmod.h"
#include "dmosi.h"
#include "clock_wait.h"
#include "registers.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

volatile uint32_t test_rcc[64], test_nvic[64], test_pwr, test_rtc[64];
volatile uint8_t test_priority;
struct test_semaphore { sem_t sem; };
static pthread_mutex_t critical = PTHREAD_MUTEX_INITIALIZER;
static unsigned allocations, posts, sleeps;
static bool started = true, in_interrupt, fail_allocation, early;
volatile uint32_t* test_register(uintptr_t base, unsigned off)
{
    if (base == 0x40023800u) return &test_rcc[off/4];
    if (base == 0x40007000u) return &test_pwr;
    assert(base == 0x40002800u); return &test_rtc[off/4];
}
static uint64_t nanoseconds(clockid_t id)
{
    struct timespec t; assert(clock_gettime(id, &t) == 0);
    return (uint64_t)t.tv_sec * 1000000000u + t.tv_nsec;
}
uint64_t Dmod_GetUptime(void) { return nanoseconds(CLOCK_MONOTONIC) / 1000000; }
bool Dmod_IsInsideInterrupt(void) { return in_interrupt; }
bool dmosi_is_started(void) { return started; }
void Dmod_EnterCritical(void) { assert(pthread_mutex_lock(&critical) == 0); }
void Dmod_ExitCritical(void) { assert(pthread_mutex_unlock(&critical) == 0); }
uint32_t dmosi_get_min_interrupt_priority(void) { return 0x50; }
void dmosi_thread_sleep(uint32_t ms)
{
    ++sleeps;
    struct timespec t = {ms/1000, (ms%1000)*1000000}; nanosleep(&t, NULL);
}
dmosi_semaphore_t dmosi_semaphore_create(uint32_t initial, uint32_t maximum)
{
    assert(initial == 0 && maximum == 1);
    if (fail_allocation) return NULL;
    struct test_semaphore* s = malloc(sizeof(*s)); assert(s != NULL);
    assert(sem_init(&s->sem, 0, initial) == 0); ++allocations; return s;
}
void dmosi_semaphore_destroy(dmosi_semaphore_t s)
{
    assert(sem_destroy(&s->sem) == 0); free(s);
}
static void deliver_irq(unsigned flag)
{
    Dmod_EnterCritical();
    if (flag == 2) RCC(BDCR) |= LSERDY;
    else RCC(CSR) |= LSIRDY;
    RCC(RCC_CIR) = (RCC(RCC_CIR) & RCC_IRQ_ENABLES) | flag;
    stm32_rtc_clock_irq();
    Dmod_ExitCritical();
}
int dmosi_semaphore_post(dmosi_semaphore_t s, uint32_t count)
{
    assert(count == 1); ++posts; return sem_post(&s->sem);
}
int dmosi_semaphore_wait(dmosi_semaphore_t s, uint32_t count, int32_t ms)
{
    assert(count == 1);
    if (early) { early = false; deliver_irq(2); } /* Event before blocking. */
    uint64_t deadline = nanoseconds(CLOCK_REALTIME) + (uint64_t)ms * 1000000;
    struct timespec t = {(time_t)(deadline/1000000000), (long)(deadline%1000000000)};
    int rc;
    do { rc = sem_timedwait(&s->sem, &t); } while (rc != 0 && errno == EINTR);
    return rc == 0 ? 0 : -errno;
}
static void* ready_worker(void* arg)
{
    struct timespec t = {0, 10000000}; nanosleep(&t, NULL);
    /* An unrelated ready source must not wake our waiter. */
    Dmod_EnterCritical();
    RCC(RCC_CIR) = (RCC(RCC_CIR) & RCC_IRQ_ENABLES) | 8;
    stm32_rtc_clock_irq();
    assert(posts == 0);
    Dmod_ExitCritical();
    deliver_irq((unsigned)(uintptr_t)arg); return NULL;
}
static void reset(void)
{
    memset((void*)test_rcc, 0, sizeof(test_rcc));
    memset((void*)test_nvic, 0, sizeof(test_nvic));
    test_priority = 0x20; posts = sleeps = allocations = 0;
    started = true; in_interrupt = fail_allocation = early = false;
}
int main(void)
{
    reset(); RCC(BDCR) = 3;
    assert(stm32_rtc_start_clock(DMRTC_CLOCK_LSE, 80) == 0 && allocations == 0);
    puts("PASS already-ready fast path");
    for (unsigned flag = 1; flag <= 2; ++flag) {
        reset(); pthread_t worker;
        assert(pthread_create(&worker, NULL, ready_worker, (void*)(uintptr_t)flag) == 0);
        assert(stm32_rtc_start_clock(flag == 2 ? DMRTC_CLOCK_LSE : DMRTC_CLOCK_LSI, 200) == 0);
        assert(pthread_join(worker, NULL) == 0);
        assert(posts == 1 && !(RCC(RCC_CIR) & (flag << 8)));
        puts(flag == 2 ? "PASS LSE IRQ wake and unrelated IRQ ignored" : "PASS LSI IRQ wake");
    }
    reset(); early = true;
    assert(stm32_rtc_start_clock(DMRTC_CLOCK_LSE, 80) == 0 && posts == 1);
    puts("PASS event before blocked wait");
    reset(); uint64_t wall = Dmod_GetUptime(), cpu = nanoseconds(CLOCK_THREAD_CPUTIME_ID);
    assert(stm32_rtc_start_clock(DMRTC_CLOCK_LSE, 80) == -ETIMEDOUT);
    cpu = nanoseconds(CLOCK_THREAD_CPUTIME_ID) - cpu; wall = Dmod_GetUptime() - wall;
    assert(wall >= 70 && cpu < wall * 1000000 / 4);
    assert(!(RCC(RCC_CIR) & (2u << 8)) && test_nvic[0x80/4] == RCC_IRQ_BIT);
    deliver_irq(2); assert(posts == 0); /* No post to a destroyed semaphore. */
    printf("PASS timeout sleeps: wall=%llu ms CPU=%llu us; late IRQ safe\n",
        (unsigned long long)wall, (unsigned long long)(cpu/1000));
    reset(); RCC(RCC_CIR) = 8u << 8; test_nvic[0] = RCC_IRQ_BIT; test_priority = 0xa0;
    early = true; assert(stm32_rtc_start_clock(DMRTC_CLOCK_LSE, 80) == 0);
    assert((RCC(RCC_CIR) & RCC_IRQ_ENABLES) == (8u << 8) && test_priority == 0xa0);
    assert(test_nvic[0x80/4] == 0 && !(RCC(RCC_CIR) & (8u << 16)));
    puts("PASS shared RCC source and NVIC state preserved");
    reset(); RCC(RCC_CIR) = 2u << 8;
    assert(stm32_rtc_start_clock(DMRTC_CLOCK_LSE, 80) == -EBUSY);
    assert(RCC(RCC_CIR) == (2u << 8));
    reset(); test_nvic[0] = RCC_IRQ_BIT; test_priority = 0x10;
    assert(stm32_rtc_start_clock(DMRTC_CLOCK_LSE, 80) == -EBUSY && test_priority == 0x10);
    puts("PASS existing owner and unsafe IRQ priority rejected");
    reset(); fail_allocation = true;
    assert(stm32_rtc_start_clock(DMRTC_CLOCK_LSE, 80) == -ENOMEM && RCC(BDCR) == 0);
    puts("PASS allocation failure leaves oscillator untouched");
    reset(); volatile uint32_t reg = 0;
    cpu = nanoseconds(CLOCK_THREAD_CPUTIME_ID); wall = Dmod_GetUptime();
    assert(stm32_rtc_wait_register(&reg, 1, 1) == -ETIMEDOUT);
    wall = Dmod_GetUptime() - wall; cpu = nanoseconds(CLOCK_THREAD_CPUTIME_ID) - cpu;
    assert(sleeps > 0 && wall >= REGISTER_TIMEOUT_MS && cpu < wall * 1000000 / 4);
    printf("PASS register timeout sleeps: wall=%llu ms CPU=%llu us\n",
        (unsigned long long)wall, (unsigned long long)(cpu/1000));
    reset(); started = false;
    assert(stm32_rtc_start_clock(DMRTC_CLOCK_LSE, 80) == -ENOTSUP);
    assert(stm32_rtc_wait_register(&reg, 1, 1) == -ENOTSUP);
    reset(); in_interrupt = true;
    assert(stm32_rtc_start_clock(DMRTC_CLOCK_LSE, 80) == -ENOTSUP);
    assert(stm32_rtc_wait_register(&reg, 1, 1) == -ENOTSUP);
    puts("PASS blocking calls reject pre-scheduler and ISR contexts");
    return 0;
}
