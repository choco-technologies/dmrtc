#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "dmrtc.h"
#include <errno.h>
#include <string.h>
static void print_calendar(const dmrtc_calendar_t* c)
{
    Dmod_Printf("%04u-%02u-%02uT%02u:%02u:%02u weekday=%u\n", (unsigned)c->year,
        (unsigned)c->month, (unsigned)c->day, (unsigned)c->hour,
        (unsigned)c->minute, (unsigned)c->second, (unsigned)c->weekday);
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
static int parse(const char* s, dmrtc_calendar_t* c)
{
    if (strlen(s) != 19 || s[4] != '-' || s[7] != '-' || s[10] != 'T' ||
        s[13] != ':' || s[16] != ':') return -EINVAL;
    int values[] = {number(s,4),number(s+5,2),number(s+8,2),number(s+11,2),
        number(s+14,2),number(s+17,2)};
    for (unsigned i = 0; i < 6; ++i) if (values[i] < 0) return -EINVAL;
    *c = (dmrtc_calendar_t){(uint16_t)values[0], (uint8_t)values[1], (uint8_t)values[2],
        (uint8_t)values[3], (uint8_t)values[4], (uint8_t)values[5], 0};
    return dmrtc_normalize(c);
}
int main(int argc, char* argv[])
{
    dmrtc_t ctx = dmrtc_default();
    if (ctx == NULL) { Dmod_Printf("rtc: no configured device\n"); return 1; }
    int rc = -EINVAL;
    dmrtc_calendar_t c;
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "get") == 0)) {
        rc = dmrtc_get(ctx, &c);
        if (rc == 0) print_calendar(&c);
    } else if (argc == 2 && strcmp(argv[1], "status") == 0) {
        dmrtc_status_t s;
        rc = dmrtc_status(ctx, &s);
        if (rc == 0) Dmod_Printf("running=%u calendar_set=%u clock=%s nominal_hz=%u\n",
            s.running, s.calendar_set, s.clock == DMRTC_CLOCK_LSE ? "lse" : "lsi", (unsigned)s.clock_hz);
    } else if (argc == 2 && strcmp(argv[1], "invalidate") == 0) {
        rc = dmrtc_invalidate(ctx);
    } else if (argc == 3 && strcmp(argv[1], "set") == 0) {
        rc = parse(argv[2], &c);
        if (rc == 0) rc = dmrtc_set(ctx, &c);
    } else Dmod_Printf("rtc [get|status|invalidate|set YYYY-MM-DDTHH:MM:SS]\n");
    if (rc != 0) Dmod_Printf("rtc: error %d\n", rc);
    return rc == 0 ? 0 : 1;
}
