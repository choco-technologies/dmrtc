#ifndef TEST_DMOSI_H
#define TEST_DMOSI_H
#include <stdint.h>
#include <stdbool.h>
typedef struct test_semaphore* dmosi_semaphore_t;
dmosi_semaphore_t dmosi_semaphore_create(uint32_t, uint32_t);
void dmosi_semaphore_destroy(dmosi_semaphore_t);
int dmosi_semaphore_wait(dmosi_semaphore_t, uint32_t, int32_t);
int dmosi_semaphore_post(dmosi_semaphore_t, uint32_t);
uint32_t dmosi_get_min_interrupt_priority(void);
void dmosi_thread_sleep(uint32_t);
bool dmosi_is_started(void);
#endif
