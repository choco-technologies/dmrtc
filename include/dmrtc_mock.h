#ifndef DMRTC_MOCK_H
#define DMRTC_MOCK_H
#include "dmrtc_port.h"
/* Test-only port: preserve state across release/acquire, inject next set failure. */
dmod_dmrtc_port_api(1.0, void, _mock_fail_set, (int error));
#endif
