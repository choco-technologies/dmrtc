#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "dmdrvi_ioctl.h"
#include <errno.h>
#include <string.h>

static void print_time(const dmdrvi_rtc_time_t* c)
{
    Dmod_Printf("%04d-%02u-%02uT%02u:%02u:%02u", (int)c->year,
        (unsigned)c->month, (unsigned)c->day, (unsigned)c->hour,
        (unsigned)c->minute, (unsigned)c->second);
    if (c->nanosecond != 0) Dmod_Printf(".%09u", (unsigned)c->nanosecond);
    Dmod_Printf(" weekday=%u\n", (unsigned)c->weekday);
}
static int number(const char* s, unsigned count)
{
    int value = 0;
    while (count-- != 0) {
        if (*s < '0' || *s > '9') return -1;
        value = value * 10 + *s++ - '0';
    }
    return value;
}
static int parse(const char* s, dmdrvi_rtc_time_t* c)
{
    size_t len = strlen(s);
    if (len < 19 || len > 29 || s[4] != '-' || s[7] != '-' || s[10] != 'T' ||
        s[13] != ':' || s[16] != ':') return -EINVAL;
    int v[] = {number(s,4),number(s+5,2),number(s+8,2),number(s+11,2),
        number(s+14,2),number(s+17,2)};
    if (v[0] < 1 || v[1] < 1 || v[1] > 12 || v[2] < 1 || v[2] > 31 ||
        v[3] < 0 || v[3] > 23 || v[4] < 0 || v[4] > 59 || v[5] < 0 || v[5] > 59)
        return -EINVAL;
    memset(c, 0, sizeof(*c));
    c->year=v[0]; c->month=(uint8_t)v[1]; c->day=(uint8_t)v[2];
    c->hour=(uint8_t)v[3]; c->minute=(uint8_t)v[4]; c->second=(uint8_t)v[5];
    if (len > 19) {
        if (s[19] != '.' || len == 20) return -EINVAL;
        int fraction = number(s + 20, (unsigned)(len - 20));
        if (fraction < 0) return -EINVAL;
        c->nanosecond = (uint32_t)fraction;
        for (size_t n = len - 20; n < 9; ++n) c->nanosecond *= 10;
    }
    return 0; /* Complete Gregorian/range/precision validation belongs to the device. */
}
static int execute(void* file, int argc, char** argv)
{
    const char* action = argc == 0 ? "get" : argv[0];
    if ((argc == 0 || argc == 1) && strcmp(action,"get") == 0) {
        dmdrvi_rtc_time_t time;
        int rc = Dmod_Ioctl(file, DMDRVI_IOCTL_RTC_GET_TIME, &time);
        if (rc == 0) print_time(&time);
        return rc;
    }
    if (argc == 1 && strcmp(action,"status") == 0) {
        dmdrvi_rtc_status_t status;
        int rc = Dmod_Ioctl(file, DMDRVI_IOCTL_RTC_GET_STATUS, &status);
        dmdrvi_rtc_info_t info;
        if (rc == 0) rc = Dmod_Ioctl(file, DMDRVI_IOCTL_RTC_GET_INFO, &info);
        if (rc == 0) {
            Dmod_Printf("running=%u calendar_set=%u ",
                !!(status.flags & DMDRVI_RTC_STATUS_RUNNING),
                !!(status.flags & DMDRVI_RTC_STATUS_TIME_SET));
            if (info.capabilities & DMDRVI_RTC_CAP_LOSS_DETECTION)
                Dmod_Printf("time_lost=%u\n", !!(status.flags & DMDRVI_RTC_STATUS_TIME_LOST));
            else Dmod_Printf("time_lost=unknown\n");
        }
        return rc;
    }
    if (argc == 1 && strcmp(action,"info") == 0) {
        dmdrvi_rtc_info_t info;
        int rc = Dmod_Ioctl(file, DMDRVI_IOCTL_RTC_GET_INFO, &info);
        if (rc == 0) Dmod_Printf("years=%d..%d resolution_ns=%llu writable=%u invalidatable=%u loss_detection=%u\n",
            (int)info.min_year, (int)info.max_year, (unsigned long long)info.resolution_ns,
            !!(info.capabilities & DMDRVI_RTC_CAP_SET_TIME),
            !!(info.capabilities & DMDRVI_RTC_CAP_INVALIDATE),
            !!(info.capabilities & DMDRVI_RTC_CAP_LOSS_DETECTION));
        return rc;
    }
    if (argc == 1 && strcmp(action,"invalidate") == 0)
        return Dmod_Ioctl(file, DMDRVI_IOCTL_RTC_INVALIDATE, NULL);
    if (argc == 2 && strcmp(action,"set") == 0) {
        dmdrvi_rtc_time_t time;
        int rc = parse(argv[1], &time);
        return rc != 0 ? rc : Dmod_Ioctl(file, DMDRVI_IOCTL_RTC_SET_TIME, &time);
    }
    return -EINVAL;
}
int main(int argc, char* argv[])
{
    const char* path = "/dev/rtc";
    int first = 1;
    if (argc > 1 && strcmp(argv[1],"-d") == 0) {
        if (argc < 3) return 1;
        path = argv[2]; first = 3;
    }
    const char* action = first < argc ? argv[first] : "get";
    bool write = strcmp(action,"set") == 0 || strcmp(action,"invalidate") == 0;
    void* file = Dmod_FileOpen(path, write ? "r+b" : "rb");
    if (file == NULL) { Dmod_Printf("rtc: cannot open %s\n", path); return 1; }
    int rc = execute(file, argc-first, argv+first);
    Dmod_FileClose(file);
    if (rc != 0) {
        Dmod_Printf("rtc: error %d\n", rc);
        if (rc == -EINVAL) Dmod_Printf("rtc [-d DEVICE] [get|status|info|invalidate|set YYYY-MM-DDTHH:MM:SS[.fraction]]\n");
    }
    return rc == 0 ? 0 : 1;
}
