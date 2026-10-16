/* Compile the actual client against an independent VFS RTC implementation.
 * This test intentionally has no dmrtc headers, contexts or exported API. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define main rtc_client_main
#include "../../apps/rtc/rtc.c"
#undef main

static char output[2048], opened_path[64], opened_mode[8];
static unsigned closes, sets;
static int device;
static uint32_t capabilities;
static dmdrvi_rtc_time_t calendar;
int Dmod_Printf(const char* format, ...)
{
    va_list args; va_start(args, format);
    int n = vsnprintf(output + strlen(output), sizeof(output)-strlen(output), format, args);
    va_end(args); return n;
}
void* Dmod_FileOpen(const char* path, const char* mode)
{
    snprintf(opened_path, sizeof(opened_path), "%s", path);
    snprintf(opened_mode, sizeof(opened_mode), "%s", mode);
    return &device;
}
int Dmod_FileClose(void* file) { assert(file == &device); ++closes; return 0; }
int Dmod_Ioctl(void* file, int command, void* arg)
{
    assert(file == &device);
    switch (command) {
        case DMDRVI_IOCTL_RTC_GET_INFO:
            *(dmdrvi_rtc_info_t*)arg = (dmdrvi_rtc_info_t){.resolution_ns=1,
                .capabilities=capabilities, .min_year=1900, .max_year=2199}; return 0;
        case DMDRVI_IOCTL_RTC_GET_STATUS:
            *(dmdrvi_rtc_status_t*)arg = (dmdrvi_rtc_status_t){
                .flags=DMDRVI_RTC_STATUS_RUNNING | DMDRVI_RTC_STATUS_TIME_SET}; return 0;
        case DMDRVI_IOCTL_RTC_GET_TIME: *(dmdrvi_rtc_time_t*)arg = calendar; return 0;
        case DMDRVI_IOCTL_RTC_SET_TIME:
            if (!(capabilities & DMDRVI_RTC_CAP_SET_TIME)) return -ENOTSUP;
            calendar = *(dmdrvi_rtc_time_t*)arg; ++sets; return 0;
        default: return -ENOTTY;
    }
}
static int run(unsigned count, char** args)
{
    output[0]=opened_path[0]=opened_mode[0]=0; closes=0;
    int result=rtc_client_main((int)count,args);
    assert(closes==1); return result;
}
int main(void)
{
    calendar=(dmdrvi_rtc_time_t){.year=1999,.month=12,.day=31,.hour=23,
        .minute=59,.second=59,.weekday=5,.nanosecond=123456789};
    char* get[]={"rtc","-d","/dev/other-clock","get"};
    assert(run(4,get)==0 && strcmp(opened_path,"/dev/other-clock")==0);
    assert(strcmp(opened_mode,"rb")==0 && strstr(output,"1999-12-31T23:59:59.123456789"));
    puts("PASS generic read-only RTC outside STM32 year/precision range");
    char* info[]={"rtc","info"}; assert(run(2,info)==0);
    assert(strcmp(opened_path,"/dev/rtc")==0 && strstr(output,"1900..2199") && strstr(output,"writable=0"));
    char* status[]={"rtc","status"}; assert(run(2,status)==0 && strstr(output,"time_lost=unknown"));
    puts("PASS device info and unknown loss-detection capability");
    char* set[]={"rtc","-d","/dev/other-clock","set","1901-01-01T12:34:56.25"};
    assert(run(5,set)==1 && sets==0);
    capabilities=DMDRVI_RTC_CAP_SET_TIME | DMDRVI_RTC_CAP_LOSS_DETECTION;
    assert(run(5,set)==0 && sets==1 && strcmp(opened_mode,"r+b")==0);
    assert(calendar.year==1901 && calendar.nanosecond==250000000 && calendar.reserved==0);
    assert(run(2,status)==0 && strstr(output,"time_lost=0"));
    puts("PASS optional SET_TIME and fractional-second input");
    set[4]="1901-01-01T12:34:56.1234567890";
    assert(run(5,set)==1 && sets==1);
    set[4]="1901-01-01T12:34:56.x"; assert(run(5,set)==1 && sets==1);
    puts("PASS invalid fractions rejected before issuing SET_TIME");
    return 0;
}
