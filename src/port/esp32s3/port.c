#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "dmrtc_port.h"
#include <errno.h>
#include <stdint.h>
#include <string.h>

/*
 * dmrtc_port for the ESP32-S3: the calendar is kept by the RTC timer of the chip (RTC_CNTL, 48 bits, it counts the slow clock of the RTC and
 * is not touched by a reset of the CPU or of the system), the point of reference by a record in the RTC slow memory (it keeps its content
 * through the resets, too):
 *
 *     calendar = the calendar set + (RTC timer now - RTC timer when it was set) / (frequency of the slow clock)
 *
 * The slow clock of the chip is an RC oscillator (about 136 kHz, +-5 %) when no 32 kHz crystal is fitted, so its frequency is measured at
 * the moment the calendar is set, against the crystal of the CPU (TIMG0, the calibration of the RTC clock): the error is then the one of
 * the measurement (below 0.1 %), not of the oscillator. What the chip cannot do is to keep the time when its power is gone (nor on a
 * power-on reset: the RTC timer starts from 0 and the record is not valid then): the clock then says "not set" until it is set again.
 */

#define RTC_CNTL_BASE           0x60008000u
#define RTC_CNTL_TIME_UPDATE    (RTC_CNTL_BASE + 0x0Cu)     /* bit 31: latch the timer into TIME_LOW0/HIGH0 */
#define RTC_CNTL_TIME_LOW0      (RTC_CNTL_BASE + 0x10u)
#define RTC_CNTL_TIME_HIGH0     (RTC_CNTL_BASE + 0x14u)     /* 16 bits */
#define RTC_CNTL_CLK_CONF       (RTC_CNTL_BASE + 0x74u)     /* bits 31:30 ANA_CLK_RTC_SEL: 0 RC slow, 1 XTAL32K, 2 RC fast / 256 */

#define TIMG0_BASE              0x6001F000u
#define TIMG_RTCCALICFG         (TIMG0_BASE + 0x68u)        /* 12 cycling, 14:13 clock, 15 ready, 30:16 slow cycles, 31 start */
#define TIMG_RTCCALICFG1        (TIMG0_BASE + 0x6Cu)        /* 31:7 value: cycles of the crystal for the slow cycles */
#define XTAL_HZ                 40000000u

#define RECORD_ADDRESS          0x50001FC0u                 /* the end of the 8 KB of the RTC slow memory */
#define RECORD_MAGIC            0x52544333u

#define CALIBRATION_CYCLES      1024u
#define SLOW_HZ_MIN             20000u
#define SLOW_HZ_MAX             200000u

typedef struct {
    uint32_t magic;
    uint32_t epoch;         /* seconds since 2000-01-01T00:00:00 when the calendar was set */
    uint32_t ticks_lo;      /* the RTC timer then */
    uint32_t ticks_hi;
    uint32_t hz;            /* the slow clock, measured then */
    uint32_t check;
} record_t;

static bool acquired;
static dmrtc_config_t cfg;

static inline volatile uint32_t *reg(uintptr_t address) { return (volatile uint32_t *)address; }
static inline volatile record_t *record(void) { return (volatile record_t *)RECORD_ADDRESS; }

static uint32_t record_check(const record_t *r)
{
    return r->magic ^ (r->epoch * 3u) ^ (r->ticks_lo * 5u) ^ (r->ticks_hi * 7u) ^ (r->hz * 11u) ^ 0xA5A5A5A5u;
}

/** 64-bit / 32-bit division by shifting: the modules have no libgcc */
static uint64_t div64(uint64_t n, uint32_t d)
{
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) { r -= d; q |= (uint64_t)1 << i; }
    }
    return q;
}

static uint64_t ticks_now(void)
{
    *reg(RTC_CNTL_TIME_UPDATE) = 1u << 31;
    for (volatile int i = 0; i < 1000 && (*reg(RTC_CNTL_TIME_UPDATE) & (1u << 30)) == 0; i++) {
        /* TIME_VALID: the registers hold the latched value (when the bit is not there, a short wait does it) */
    }
    uint32_t lo = *reg(RTC_CNTL_TIME_LOW0);
    uint32_t hi = *reg(RTC_CNTL_TIME_HIGH0) & 0xFFFFu;
    return ((uint64_t)hi << 32) | lo;
}

/** The frequency of the slow clock of the RTC in Hz, 0 when it could not be measured */
static uint32_t measure_slow_hz(void)
{
    *reg(TIMG_RTCCALICFG) = 0;                                              /* clock 0: the slow clock selected for the RTC */
    *reg(TIMG_RTCCALICFG) = CALIBRATION_CYCLES << 16;
    *reg(TIMG_RTCCALICFG) |= 1u << 31;
    uint64_t start = Dmod_GetUptime();
    while ((*reg(TIMG_RTCCALICFG) & (1u << 15)) == 0 && Dmod_GetUptime() - start < 100u) {
    }
    if ((*reg(TIMG_RTCCALICFG) & (1u << 15)) == 0) {
        *reg(TIMG_RTCCALICFG) &= ~(1u << 31);
        return 0;
    }
    uint32_t value = *reg(TIMG_RTCCALICFG1) >> 7;
    *reg(TIMG_RTCCALICFG) &= ~(1u << 31);
    if (value == 0) {
        return 0;
    }
    uint32_t hz = (uint32_t)div64((uint64_t)XTAL_HZ * CALIBRATION_CYCLES + value / 2u, value);
    return (hz >= SLOW_HZ_MIN && hz <= SLOW_HZ_MAX) ? hz : 0;
}

static bool is_leap(unsigned year) { return year % 4u == 0u; }       /* 2000..2099 has no century exceptions */

static uint16_t days_before[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

static uint32_t to_epoch(const dmrtc_calendar_t *c)
{
    unsigned y = c->year - 2000u;
    uint32_t days = y * 365u + (y + 3u) / 4u + days_before[c->month - 1] + (is_leap(c->year) && c->month > 2 ? 1u : 0u) + (c->day - 1u);
    return days * 86400u + c->hour * 3600u + c->minute * 60u + c->second;
}

static void from_epoch(uint32_t epoch, dmrtc_calendar_t *c)
{
    uint32_t days = epoch / 86400u, rest = epoch % 86400u;
    c->hour = (uint8_t)(rest / 3600u);
    c->minute = (uint8_t)(rest % 3600u / 60u);
    c->second = (uint8_t)(rest % 60u);
    unsigned year = 2000u;
    for (;;) {
        unsigned n = is_leap(year) ? 366u : 365u;
        if (days < n) break;
        days -= n;
        year++;
    }
    unsigned month = 12;
    while (month > 1) {
        unsigned before = days_before[month - 1] + (is_leap(year) && month > 2 ? 1u : 0u);
        if (days >= before) break;
        month--;
    }
    c->year = (uint16_t)year;
    c->month = (uint8_t)month;
    c->day = (uint8_t)(days - days_before[month - 1] - (is_leap(year) && month > 2 ? 1u : 0u) + 1u);
    c->weekday = 0;                                                         /* the core computes it (dmrtc_normalize) */
}

/** The epoch now, or an error: -ENODATA when the calendar was not set (or the RTC timer started over: the power was gone) */
static int epoch_now(uint32_t *epoch, uint32_t *hz)
{
    record_t r;
    const volatile record_t *src = record();
    r.magic = src->magic; r.epoch = src->epoch; r.ticks_lo = src->ticks_lo; r.ticks_hi = src->ticks_hi;
    r.hz = src->hz; r.check = src->check;
    if (r.magic != RECORD_MAGIC || r.check != record_check(&r) || r.hz < SLOW_HZ_MIN || r.hz > SLOW_HZ_MAX) {
        return -ENODATA;
    }
    uint64_t was = ((uint64_t)r.ticks_hi << 32) | r.ticks_lo;
    uint64_t now = ticks_now();
    if (now < was) {
        return -ENODATA;
    }
    uint64_t seconds = div64(now - was, r.hz);
    if (seconds > 0xFFFFFFFFu - r.epoch) {
        return -ENODATA;
    }
    *epoch = r.epoch + (uint32_t)seconds;
    if (hz != NULL) *hz = r.hz;
    return 0;
}

dmod_dmrtc_port_api_declaration(1.0, int, _acquire, (const dmrtc_config_t *config))
{
    if (config == NULL || config->instance != 0) return -EINVAL;
    if (acquired) return -EBUSY;
    cfg = *config;
    acquired = true;
    return 0;
}

dmod_dmrtc_port_api_declaration(1.0, int, _release, (void))
{
    if (!acquired) return -ENODEV;
    acquired = false;                                                        /* the clock goes on */
    return 0;
}

dmod_dmrtc_port_api_declaration(1.0, int, _get, (dmrtc_calendar_t *calendar))
{
    if (calendar == NULL) return -EINVAL;
    memset(calendar, 0, sizeof(*calendar));
    if (!acquired) return -ENODEV;
    uint32_t epoch;
    int rc = epoch_now(&epoch, NULL);
    if (rc != 0) return rc;
    from_epoch(epoch, calendar);
    return 0;
}

dmod_dmrtc_port_api_declaration(1.0, int, _set, (const dmrtc_calendar_t *calendar))
{
    if (!acquired) return -ENODEV;
    if (calendar == NULL) return -EINVAL;
    volatile record_t *rec = record();
    rec->magic = 0;                                                         /* not set while it is rewritten */
    uint32_t hz = measure_slow_hz();
    if (hz == 0) {
        return -EIO;
    }
    record_t r = { RECORD_MAGIC, to_epoch(calendar), 0, 0, hz, 0 };
    uint64_t ticks = ticks_now();
    r.ticks_lo = (uint32_t)ticks;
    r.ticks_hi = (uint32_t)(ticks >> 32);
    r.check = record_check(&r);
    rec->epoch = r.epoch; rec->ticks_lo = r.ticks_lo; rec->ticks_hi = r.ticks_hi; rec->hz = r.hz; rec->check = r.check;
    rec->magic = r.magic;                                                   /* the marker is the last */
    return 0;
}

dmod_dmrtc_port_api_declaration(1.0, int, _status, (dmrtc_status_t *status))
{
    if (status == NULL) return -EINVAL;
    if (!acquired) return -ENODEV;
    uint32_t epoch, hz = cfg.clock_hz;
    bool set = epoch_now(&epoch, &hz) == 0;
    *status = (dmrtc_status_t){ true, set, cfg.clock, hz };
    return 0;
}

dmod_dmrtc_port_api_declaration(1.0, int, _invalidate, (void))
{
    if (!acquired) return -ENODEV;
    record()->magic = 0;
    return 0;
}

int dmod_init(const Dmod_Config_t *config) { (void)config; return 0; }
int dmod_deinit(void) { return acquired ? -EBUSY : 0; }
