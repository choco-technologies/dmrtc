#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "dmrtc_port.h"
#include "../stm32_common/stm32_common.h"
int dmod_init(const Dmod_Config_t* config) { (void)config; return 0; }
int dmod_deinit(void) { return stm32_rtc_port_deinit(); }
