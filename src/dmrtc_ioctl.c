#include "dmod.h"
#include "dmrtc_internal.h"
#include "dmdrvi.h"
#include <errno.h>
#include <string.h>

static int get_time(dmrtc_t ctx, dmdrvi_rtc_time_t* out)
{
    if (out == NULL) return -EINVAL;
    memset(out, 0, sizeof(*out));
    dmrtc_calendar_t c;
    int rc = dmrtc_get(ctx, &c);
    if (rc == 0) {
        out->year = c.year; out->month = c.month; out->day = c.day;
        out->hour = c.hour; out->minute = c.minute; out->second = c.second;
        out->weekday = c.weekday;
    }
    return rc;
}
static int set_time(dmrtc_t ctx, const dmdrvi_rtc_time_t* in)
{
    if (in == NULL || in->reserved != 0 || in->year < 1 || in->year > 9999 ||
        in->nanosecond >= 1000000000u) return -EINVAL;
    if (in->year < 2000 || in->year > 2099) return -ERANGE;
    dmrtc_calendar_t c = {(uint16_t)in->year, in->month, in->day,
        in->hour, in->minute, in->second, 0};
    if (dmrtc_normalize(&c) != 0) return -EINVAL;
    if (in->nanosecond != 0) return -ERANGE;
    return dmrtc_set(ctx, &c);
}
static int get_info(dmdrvi_rtc_info_t* out)
{
    if (out == NULL) return -EINVAL;
    memset(out, 0, sizeof(*out));
    out->resolution_ns = 1000000000u;
    out->capabilities = DMDRVI_RTC_CAP_SET_TIME | DMDRVI_RTC_CAP_INVALIDATE;
    out->min_year = 2000; out->max_year = 2099;
    /* STM32F4/F7 cannot attest calendar continuity across all supply failures. */
    return 0;
}
static int get_status(dmrtc_t ctx, dmdrvi_rtc_status_t* out)
{
    if (out == NULL) return -EINVAL;
    memset(out, 0, sizeof(*out));
    dmrtc_status_t s;
    int rc = dmrtc_status(ctx, &s);
    if (rc == 0) {
        if (s.running) out->flags |= DMDRVI_RTC_STATUS_RUNNING;
        if (s.calendar_set) out->flags |= DMDRVI_RTC_STATUS_TIME_SET;
    }
    return rc;
}
int dmrtc_device_ioctl(dmrtc_t ctx, int flags, int command, void* arg)
{
    bool write = command == DMDRVI_IOCTL_RTC_SET_TIME || command == DMDRVI_IOCTL_RTC_INVALIDATE;
    bool read = command == DMDRVI_IOCTL_RTC_GET_TIME || command == DMDRVI_IOCTL_RTC_GET_STATUS ||
        command == DMDRVI_IOCTL_RTC_GET_INFO;
    if (!write && !read) return -ENOTTY;
    if (write && !(flags & (DMDRVI_O_WRONLY | DMDRVI_O_RDWR))) return -EACCES;
    if (read && !(flags & (DMDRVI_O_RDONLY | DMDRVI_O_RDWR))) return -EACCES;
    switch (command) {
        case DMDRVI_IOCTL_RTC_GET_TIME: return get_time(ctx, arg);
        case DMDRVI_IOCTL_RTC_SET_TIME: return set_time(ctx, arg);
        case DMDRVI_IOCTL_RTC_GET_STATUS: return get_status(ctx, arg);
        case DMDRVI_IOCTL_RTC_GET_INFO: return get_info(arg);
        default: return arg == NULL ? dmrtc_invalidate(ctx) : -EINVAL;
    }
}
