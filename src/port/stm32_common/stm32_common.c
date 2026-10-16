#include "dmod.h"
#include "dmrtc_port.h"
#include "clock_wait.h"
#include "registers.h"
#include "stm32_common.h"
#include <errno.h>
#include <string.h>

static bool acquired;
static dmrtc_config_t cfg;
struct backup_guard { bool power, writable; };

static void backup_close(struct backup_guard* g)
{
    RTC(WPR) = 0xff;
    Dmod_EnterCritical();
    if (!g->writable) PWR &= ~DBP;
    if (!g->power) RCC(APB1ENR) &= ~PWREN;
    Dmod_ExitCritical();
}
static int backup_open(struct backup_guard* g)
{
    Dmod_EnterCritical();
    g->power = (RCC(APB1ENR) & PWREN) != 0;
    RCC(APB1ENR) |= PWREN;
    (void)RCC(APB1ENR); /* Peripheral clock enable read-back delay. */
    g->writable = (PWR & DBP) != 0;
    PWR |= DBP;
    Dmod_ExitCritical();
    int rc = stm32_rtc_wait_register(&PWR, DBP, DBP);
    if (rc != 0) { backup_close(g); return rc; }
    RTC(WPR) = 0xca; RTC(WPR) = 0x53;
    return 0;
}
static void clear_marker(void)
{
    BKP(cfg.backup_register) = 0;
    BKP(cfg.backup_register + 1) = 0;
}
static bool marked(void)
{
    return BKP(cfg.backup_register) == MARKER &&
        BKP(cfg.backup_register + 1) == (uint32_t)~MARKER;
}
static bool running(void)
{
    uint32_t bdcr = RCC(BDCR);
    if (!(bdcr & RTCEN) || ((bdcr & RTCSEL) >> 8) != (uint32_t)cfg.clock ||
        (RTC(ISR) & INIT)) return false;
    return cfg.clock == DMRTC_CLOCK_LSE ? (bdcr & LSERDY) != 0 : (RCC(CSR) & LSIRDY) != 0;
}
static int init_enter(void)
{
    RTC(ISR) |= INIT;
    int rc = stm32_rtc_wait_register(&RTC(ISR), INITF, INITF);
    if (rc != 0) RTC(ISR) &= ~INIT;
    return rc;
}
static int setup_clock(void)
{
    uint32_t select = (uint32_t)cfg.clock << 8;
    uint32_t old = RCC(BDCR) & RTCSEL;
    /* Changing a selected source requires destructive BDRST: refuse it. */
    if (old != 0 && old != select) return -EBUSY;
    if (cfg.clock == DMRTC_CLOCK_LSE && (RCC(BDCR) & LSEBYP)) return -EBUSY;
    int rc = stm32_rtc_start_clock(cfg.clock, cfg.timeout_ms);
    if (rc != 0) return rc;
    RCC(BDCR) = (RCC(BDCR) & ~RTCSEL) | select | RTCEN;
    uint32_t prescaler = (127u << 16) | (cfg.clock_hz / 128 - 1);
    /* INITS is zero for year 2000: our persistent marker is authoritative. */
    if ((RTC(ISR) & INITS) || marked()) {
        if (RTC(PRER) != prescaler || (RTC(CR) & FMT)) return -EBUSY;
    } else {
        rc = init_enter();
        if (rc != 0) return rc;
        clear_marker();
        RTC(CR) &= ~FMT;
        RTC(PRER) = prescaler;
        RTC(ISR) &= ~INIT;
        rc = stm32_rtc_wait_register(&RTC(ISR), INITF, 0);
        if (rc != 0) return rc;
    }
    /* Direct calendar reads avoid shadow-register locking errata. */
    RTC(CR) |= BYPSHAD;
    return 0;
}
dmod_dmrtc_port_api_declaration(1.0, int, _acquire, (const dmrtc_config_t* config))
{
    if (config == NULL || config->instance != 0) return -EINVAL;
    if (acquired) return -EBUSY;
    if (config->backup_register >= 19 || config->timeout_ms == 0 || config->timeout_ms > 10000 ||
        config->clock_hz < 128 || config->clock_hz > 65536 || config->clock_hz % 128 != 0 ||
        (config->clock != DMRTC_CLOCK_LSE && config->clock != DMRTC_CLOCK_LSI)) return -EINVAL;
    cfg = *config;
    struct backup_guard guard;
    int rc = backup_open(&guard);
    if (rc != 0) return rc;
    rc = setup_clock();
    backup_close(&guard);
    acquired = rc == 0;
    return rc;
}
dmod_dmrtc_port_api_declaration(1.0, int, _release, (void))
{
    if (!acquired) return -ENODEV;
    acquired = false; /* Keep oscillator, RTCEN, calendar and backup registers. */
    return 0;
}
static uint32_t bcd(unsigned n) { return (n / 10) * 16 + n % 10; }
static int unbcd(uint32_t n)
{
    if ((n & 15) > 9 || (n >> 4) > 9) return -1;
    return (int)((n >> 4) * 10 + (n & 15));
}
static int decode(uint32_t tr, uint32_t dr, dmrtc_calendar_t* c)
{
    int year = unbcd((dr >> 16) & 0xff);
    int month = unbcd((dr >> 8) & 0x1f);
    int day = unbcd(dr & 0x3f);
    int hour = unbcd((tr >> 16) & 0x3f);
    int minute = unbcd((tr >> 8) & 0x7f);
    int second = unbcd(tr & 0x7f);
    if (year < 0 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59 ||
        (tr & (1u << 22))) return -EIO;
    *c = (dmrtc_calendar_t){(uint16_t)(2000 + year), (uint8_t)month, (uint8_t)day,
        (uint8_t)hour, (uint8_t)minute, (uint8_t)second, (uint8_t)((dr >> 13) & 7)};
    return c->weekday >= 1 && c->weekday <= 7 ? 0 : -EIO;
}
dmod_dmrtc_port_api_declaration(1.0, int, _get, (dmrtc_calendar_t* out))
{
    if (out == NULL) return -EINVAL;
    memset(out, 0, sizeof(*out));
    if (!acquired) return -ENODEV;
    if (!running()) return -EIO;
    if (!marked()) return -ENODATA;
    for (unsigned n = 0; n < 8; ++n) {
        uint32_t tr = RTC(TR), dr = RTC(DR);
        if (tr == RTC(TR) && dr == RTC(DR)) return decode(tr, dr, out);
    }
    return -EAGAIN;
}
dmod_dmrtc_port_api_declaration(1.0, int, _set, (const dmrtc_calendar_t* c))
{
    if (c == NULL) return -EINVAL;
    if (!acquired) return -ENODEV;
    struct backup_guard guard;
    int rc = backup_open(&guard);
    if (rc != 0) return rc;
    clear_marker(); /* A failed or interrupted write cannot leave a valid marker. */
    if (!running()) rc = -EIO;
    else rc = init_enter();
    if (rc == 0) {
        RTC(TR) = bcd(c->hour) << 16 | bcd(c->minute) << 8 | bcd(c->second);
        RTC(DR) = bcd(c->year - 2000) << 16 | (uint32_t)c->weekday << 13 |
            bcd(c->month) << 8 | bcd(c->day);
        RTC(ISR) &= ~INIT;
        rc = stm32_rtc_wait_register(&RTC(ISR), INITF, 0);
        if (rc == 0) {
            BKP(cfg.backup_register) = MARKER;
            BKP(cfg.backup_register + 1) = (uint32_t)~MARKER;
        }
    }
    backup_close(&guard);
    return rc;
}
dmod_dmrtc_port_api_declaration(1.0, int, _status, (dmrtc_status_t* out))
{
    if (out == NULL) return -EINVAL;
    if (!acquired) return -ENODEV;
    *out = (dmrtc_status_t){running(), false, cfg.clock, cfg.clock_hz};
    out->calendar_set = out->running && marked();
    return 0;
}
dmod_dmrtc_port_api_declaration(1.0, int, _invalidate, (void))
{
    if (!acquired) return -ENODEV;
    struct backup_guard guard;
    int rc = backup_open(&guard);
    if (rc != 0) return rc;
    clear_marker();
    backup_close(&guard);
    return 0;
}
int stm32_rtc_port_deinit(void) { return acquired ? -EBUSY : 0; }
