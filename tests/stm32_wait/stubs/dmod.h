#ifndef TEST_DMOD_H
#define TEST_DMOD_H
#include <stdbool.h>
#include <stdint.h>
void Dmod_EnterCritical(void);
void Dmod_ExitCritical(void);
uint64_t Dmod_GetUptime(void);
bool Dmod_IsInsideInterrupt(void);
#define Dmod_Printf(...) ((void)0)
#endif
