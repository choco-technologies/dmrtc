#ifndef DMRTC_TYPES_H
#define DMRTC_TYPES_H
#include <stdint.h>
#include <stdbool.h>
typedef struct dmrtc* dmrtc_t;
typedef enum { DMRTC_CLOCK_LSE = 1, DMRTC_CLOCK_LSI = 2 } dmrtc_clock_t;
/* Civil calendar, 2000..2099. Weekday: Monday=1 .. Sunday=7; computed on set.
 * The driver does not assign a timezone or certify the source of this time. */
typedef struct {
    uint16_t year;
    uint8_t month, day, hour, minute, second, weekday;
} dmrtc_calendar_t;
typedef struct {
    uint32_t instance;
    dmrtc_clock_t clock;
    uint32_t clock_hz;
    uint32_t timeout_ms; /* Oscillator startup only; waiting task sleeps on an IRQ event. */
    uint32_t backup_register; /* Reserves this register AND the next one. */
} dmrtc_config_t;
#define DMRTC_CONFIG_DEFAULT {0, DMRTC_CLOCK_LSE, 32768, 5000, 18}
typedef struct {
    bool running;
    bool calendar_set; /* Explicitly set and structurally valid, NOT trusted UTC. */
    dmrtc_clock_t clock;
    uint32_t clock_hz; /* Configured nominal frequency, not a measurement. */
} dmrtc_status_t;

#endif
