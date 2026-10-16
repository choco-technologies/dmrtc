#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "dmrtc_port.h"
#include "dmrtc_mock.h"
#include <errno.h>
#include <string.h>
static bool acquired, valid;
static dmrtc_config_t cfg;
static dmrtc_calendar_t value;
static int failure;
dmod_dmrtc_port_api_declaration(1.0, int, _acquire, (const dmrtc_config_t* config))
{
    if (acquired) return -EBUSY;
    if (config == NULL || config->instance != 0) return -EINVAL;
    if (cfg.clock != 0 && (cfg.clock != config->clock || cfg.clock_hz != config->clock_hz))
        return -EBUSY;
    cfg = *config; acquired = true; failure = 0;
    return 0;
}
dmod_dmrtc_port_api_declaration(1.0, int, _release, (void))
{
    if (!acquired) return -ENODEV;
    acquired = false; return 0;
}
dmod_dmrtc_port_api_declaration(1.0, int, _get, (dmrtc_calendar_t* out))
{
    if (out == NULL) return -EINVAL;
    memset(out, 0, sizeof(*out));
    if (!acquired) return -ENODEV;
    if (!valid) return -ENODATA;
    *out = value; return 0;
}
dmod_dmrtc_port_api_declaration(1.0, int, _set, (const dmrtc_calendar_t* c))
{
    if (!acquired) return -ENODEV;
    valid = false;
    if (failure != 0) { int rc = failure; failure = 0; return rc; }
    value = *c; valid = true; return 0;
}
dmod_dmrtc_port_api_declaration(1.0, int, _status, (dmrtc_status_t* out))
{
    if (!acquired) return -ENODEV;
    *out = (dmrtc_status_t){true, valid, cfg.clock, cfg.clock_hz}; return 0;
}
dmod_dmrtc_port_api_declaration(1.0, int, _invalidate, (void))
{
    if (!acquired) return -ENODEV;
    valid = false; return 0;
}
dmod_dmrtc_port_api_declaration(1.0, void, _mock_fail_set, (int error)) { failure = error; }
int dmod_init(const Dmod_Config_t* config) { (void)config; return 0; }
int dmod_deinit(void) { return acquired ? -EBUSY : 0; }
