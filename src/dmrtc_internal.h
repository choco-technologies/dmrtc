#ifndef DMRTC_INTERNAL_H
#define DMRTC_INTERNAL_H
#include "dmrtc.h"
#include "dmosi.h"
#define DMRTC_MAGIC 0x52544321u
struct dmrtc {
    uint32_t magic;
    dmrtc_config_t config;
    dmosi_mutex_t lock;
    uint32_t opens;
};
int dmrtc_device_ioctl(dmrtc_t ctx, int flags, int command, void* arg);
#endif
