#ifndef DMRTC_PORT_H
#define DMRTC_PORT_H
#include "dmod_types.h"
#include "dmrtc_types.h"
#include "dmrtc_port_defs.h"
/* Core holds its mutex across every port call. Release leaves the clock and
 * calendar running. Set clears the persistent marker before modifying calendar
 * registers, and commits it only after successful initialization exit. */
dmod_dmrtc_port_api(1.0, int, _acquire, (const dmrtc_config_t* config));
dmod_dmrtc_port_api(1.0, int, _release, (void));
dmod_dmrtc_port_api(1.0, int, _get, (dmrtc_calendar_t* calendar));
dmod_dmrtc_port_api(1.0, int, _set, (const dmrtc_calendar_t* calendar));
dmod_dmrtc_port_api(1.0, int, _status, (dmrtc_status_t* status));
dmod_dmrtc_port_api(1.0, int, _invalidate, (void));
#endif
