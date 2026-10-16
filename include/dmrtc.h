#ifndef DMRTC_H
#define DMRTC_H
#include <stdint.h>
#include <stdbool.h>
#include "dmod_types.h"
#include "dmrtc_defs.h"

#include "dmrtc_types.h"

/* One hardware owner. NULL configuration selects DMRTC_CONFIG_DEFAULT.
 * Lifecycle (create/destroy/default) must be externally serialized with users.
 * Operations on a live context are serialized internally. */
dmod_dmrtc_api(1.0, dmrtc_t, _create, (const dmrtc_config_t* config));
dmod_dmrtc_api(1.0, int, _destroy, (dmrtc_t ctx));
dmod_dmrtc_api(1.0, bool, _is_valid, (dmrtc_t ctx));
/* Borrowed handle, owned by the board driver; do not destroy it. */
dmod_dmrtc_api(1.0, dmrtc_t, _default, (void));
/* Returns -ENODATA until explicitly set; output is cleared on failure. */
dmod_dmrtc_api(1.0, int, _get, (dmrtc_t ctx, dmrtc_calendar_t* calendar));
dmod_dmrtc_api(1.0, int, _set, (dmrtc_t ctx, const dmrtc_calendar_t* calendar));
dmod_dmrtc_api(1.0, int, _status, (dmrtc_t ctx, dmrtc_status_t* status));
dmod_dmrtc_api(1.0, int, _invalidate, (dmrtc_t ctx));
/* Pure validation/weekday normalization. Input is unchanged on error. */
dmod_dmrtc_api(1.0, int, _normalize, (dmrtc_calendar_t* calendar));

/* Device files implement the standard DMDRVI_IOCTL_RTC_* contract from
 * dmdrvi_ioctl.h. This direct context API is for driver/port integration. */
#endif
