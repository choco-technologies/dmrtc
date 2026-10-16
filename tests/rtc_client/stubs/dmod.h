#ifndef RTC_CLIENT_DMOD_H
#define RTC_CLIENT_DMOD_H
#include <stdbool.h>
#include <stddef.h>
int Dmod_Printf(const char* format, ...);
void* Dmod_FileOpen(const char* path, const char* mode);
int Dmod_FileClose(void* file);
int Dmod_Ioctl(void* file, int command, void* arg);
#endif
