#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "dmrtc_internal.h"
#include "dmrtc_port.h"
#include "dmdrvi.h"
#include "dmini.h"
#include <errno.h>
#include <string.h>

static dmrtc_t active;
static dmosi_mutex_t registry_lock;

dmod_dmrtc_api_declaration(1.0, int, _normalize, (dmrtc_calendar_t* c))
{
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    static const uint16_t before[] = {0,31,59,90,120,151,181,212,243,273,304,334};
    if (c == NULL || c->year < 2000 || c->year > 2099 || c->month < 1 ||
        c->month > 12 || c->hour > 23 || c->minute > 59 || c->second > 59) return -EINVAL;
    bool leap = c->year % 4 == 0; /* Calendar range excludes non-leap centuries. */
    unsigned maximum = days[c->month - 1] + (c->month == 2 && leap);
    if (c->day == 0 || c->day > maximum) return -EINVAL;
    unsigned years = c->year - 2000;
    unsigned elapsed = years * 365 + (years + 3) / 4 + before[c->month - 1] +
        (leap && c->month > 2) + c->day - 1;
    c->weekday = (uint8_t)((elapsed + 5) % 7 + 1); /* 2000-01-01 = Saturday. */
    return 0;
}

dmod_dmrtc_api_declaration(1.0, bool, _is_valid, (dmrtc_t ctx))
{
    return ctx != NULL && ctx == active && ctx->magic == DMRTC_MAGIC;
}
dmod_dmrtc_api_declaration(1.0, dmrtc_t, _default, (void)) { return active; }

static bool valid_config(const dmrtc_config_t* c)
{
    return c->instance == 0 && (c->clock == DMRTC_CLOCK_LSE || c->clock == DMRTC_CLOCK_LSI) &&
        c->clock_hz >= 128 && c->clock_hz <= 65536 && c->clock_hz % 128 == 0 &&
        c->timeout_ms > 0 && c->timeout_ms <= 10000 && c->backup_register < 19;
}
dmod_dmrtc_api_declaration(1.0, dmrtc_t, _create, (const dmrtc_config_t* config))
{
    dmrtc_config_t cfg = DMRTC_CONFIG_DEFAULT;
    if (config != NULL) cfg = *config;
    if (!valid_config(&cfg) || registry_lock == NULL) return NULL;
    if (dmosi_mutex_lock(registry_lock) != 0) return NULL;
    dmrtc_t ctx = active == NULL ? Dmod_Malloc(sizeof(*ctx)) : NULL;
    if (ctx != NULL) {
        memset(ctx, 0, sizeof(*ctx));
        ctx->config = cfg;
        ctx->lock = dmosi_mutex_create(false);
        int rc = ctx->lock == NULL ? -ENOMEM : dmrtc_port_acquire(&cfg);
        if (rc != 0) {
            DMOD_LOG_ERROR("dmrtc: acquire failed (%d)\n", rc);
            if (ctx->lock != NULL) dmosi_mutex_destroy(ctx->lock);
            Dmod_Free(ctx); ctx = NULL;
        } else { ctx->magic = DMRTC_MAGIC; active = ctx; }
    }
    dmosi_mutex_unlock(registry_lock);
    return ctx;
}
dmod_dmrtc_api_declaration(1.0, int, _destroy, (dmrtc_t ctx))
{
    if (ctx == NULL) return 0;
    if (!dmrtc_is_valid(ctx)) return -EINVAL;
    int rc = dmosi_mutex_lock(registry_lock);
    if (rc != 0) return rc;
    rc = dmosi_mutex_lock(ctx->lock);
    if (rc == 0) {
        rc = ctx->opens ? -EBUSY : dmrtc_port_release();
        dmosi_mutex_unlock(ctx->lock);
        if (rc == 0) {
            active = NULL; ctx->magic = 0;
            dmosi_mutex_destroy(ctx->lock); Dmod_Free(ctx);
        }
    }
    dmosi_mutex_unlock(registry_lock);
    return rc;
}
dmod_dmrtc_api_declaration(1.0, int, _get, (dmrtc_t ctx, dmrtc_calendar_t* out))
{
    if (out == NULL) return -EINVAL;
    memset(out, 0, sizeof(*out));
    if (!dmrtc_is_valid(ctx)) return -EINVAL;
    int rc = dmosi_mutex_lock(ctx->lock);
    if (rc != 0) return rc;
    rc = dmrtc_port_get(out);
    if (rc == 0 && dmrtc_normalize(out) != 0) rc = -EIO;
    if (rc != 0) memset(out, 0, sizeof(*out));
    dmosi_mutex_unlock(ctx->lock);
    return rc;
}
dmod_dmrtc_api_declaration(1.0, int, _set, (dmrtc_t ctx, const dmrtc_calendar_t* value))
{
    if (!dmrtc_is_valid(ctx) || value == NULL) return -EINVAL;
    dmrtc_calendar_t c = *value;
    if (dmrtc_normalize(&c) != 0) return -EINVAL;
    int rc = dmosi_mutex_lock(ctx->lock);
    if (rc != 0) return rc;
    rc = dmrtc_port_set(&c);
    dmosi_mutex_unlock(ctx->lock);
    return rc;
}
dmod_dmrtc_api_declaration(1.0, int, _status, (dmrtc_t ctx, dmrtc_status_t* out))
{
    if (out == NULL) return -EINVAL;
    memset(out, 0, sizeof(*out));
    if (!dmrtc_is_valid(ctx)) return -EINVAL;
    int rc = dmosi_mutex_lock(ctx->lock);
    if (rc != 0) return rc;
    rc = dmrtc_port_status(out);
    if (rc == 0 && out->calendar_set) {
        dmrtc_calendar_t c;
        rc = dmrtc_port_get(&c);
        if (rc == 0 && dmrtc_normalize(&c) != 0) rc = -EIO;
    }
    if (rc != 0) memset(out, 0, sizeof(*out));
    dmosi_mutex_unlock(ctx->lock);
    return rc;
}
dmod_dmrtc_api_declaration(1.0, int, _invalidate, (dmrtc_t ctx))
{
    if (!dmrtc_is_valid(ctx)) return -EINVAL;
    int rc = dmosi_mutex_lock(ctx->lock);
    if (rc != 0) return rc;
    rc = dmrtc_port_invalidate();
    dmosi_mutex_unlock(ctx->lock);
    return rc;
}
int dmod_init(const Dmod_Config_t* config)
{
    (void)config;
    registry_lock = dmosi_mutex_create(false);
    return registry_lock == NULL ? -ENOMEM : 0;
}
int dmod_deinit(void)
{
    if (active != NULL) return -EBUSY;
    dmosi_mutex_destroy(registry_lock); registry_lock = NULL;
    return 0;
}

static const char* config_section(dmini_context_t ini)
{
    if (dmini_has_key(ini, "rtc", "instance")) return "rtc";
    if (dmini_has_key(ini, "dmrtc", "instance")) return "dmrtc";
    return dmini_section_count(ini) ? dmini_section_name(ini, 0) : "rtc";
}
static bool parse_config(dmini_context_t ini, dmrtc_config_t* cfg)
{
    if (ini == NULL) return true;
    const char* s = config_section(ini);
    const char* clock = dmini_get_string(ini, s, "clock", "lse");
    if (strcmp(clock, "lse") == 0) cfg->clock = DMRTC_CLOCK_LSE;
    else if (strcmp(clock, "lsi") == 0) {
        if (!dmini_has_key(ini, s, "clock_hz")) return false;
        cfg->clock = DMRTC_CLOCK_LSI;
    }
    else return false;
    cfg->instance = (uint32_t)dmini_get_int(ini, s, "instance", 0);
    cfg->clock_hz = (uint32_t)dmini_get_int(ini, s, "clock_hz", 32768);
    cfg->timeout_ms = (uint32_t)dmini_get_int(ini, s, "timeout_ms", 5000);
    cfg->backup_register = (uint32_t)dmini_get_int(ini, s, "backup_register", 18);
    return valid_config(cfg);
}
dmod_dmdrvi_dif_api_declaration(2.0, dmrtc, dmdrvi_context_t, _create,
    (dmini_context_t ini, dmdrvi_dev_num_t* number))
{
    dmrtc_config_t cfg = DMRTC_CONFIG_DEFAULT;
    if (number == NULL || !parse_config(ini, &cfg)) return NULL;
    dmrtc_t ctx = dmrtc_create(&cfg);
    if (ctx == NULL) return NULL;
    memset(number, 0, sizeof(*number));
    number->flags = DMDRVI_NUM_MAJOR | DMDRVI_NUM_ALT_NAME;
    memcpy(number->alt_name, "rtc", sizeof("rtc"));
    return (dmdrvi_context_t)ctx;
}
dmod_dmdrvi_dif_api_declaration(2.0, dmrtc, void, _free, (dmdrvi_context_t ctx))
{
    int rc = dmrtc_destroy((dmrtc_t)ctx);
    if (rc != 0) DMOD_LOG_ERROR("dmrtc: release failed (%d)\n", rc);
}
struct rtc_file { dmrtc_t owner; int flags; };
dmod_dmdrvi_dif_api_declaration(2.0, dmrtc, void*, _open,
    (dmdrvi_context_t context, int flags, const dmdrvi_dev_num_t* number))
{
    dmrtc_t ctx = (dmrtc_t)context;
    int access = flags & (DMDRVI_O_RDONLY | DMDRVI_O_WRONLY | DMDRVI_O_RDWR);
    if (!dmrtc_is_valid(ctx) || number == NULL || number->major != 0 ||
        (access != DMDRVI_O_RDONLY && access != DMDRVI_O_WRONLY && access != DMDRVI_O_RDWR))
        return NULL;
    if (dmosi_mutex_lock(ctx->lock) != 0) return NULL;
    struct rtc_file* file = ctx->opens == UINT32_MAX ? NULL : Dmod_Malloc(sizeof(*file));
    if (file != NULL) { file->owner = ctx; file->flags = access; ++ctx->opens; }
    dmosi_mutex_unlock(ctx->lock);
    return file;
}
dmod_dmdrvi_dif_api_declaration(2.0, dmrtc, void, _close,
    (dmdrvi_context_t context, void* handle))
{
    dmrtc_t ctx = (dmrtc_t)context; struct rtc_file* file = handle;
    if (!dmrtc_is_valid(ctx) || file == NULL || file->owner != ctx) return;
    if (dmosi_mutex_lock(ctx->lock) != 0) return;
    if (ctx->opens != 0) --ctx->opens;
    file->owner = NULL; Dmod_Free(file);
    dmosi_mutex_unlock(ctx->lock);
}
dmod_dmdrvi_dif_api_declaration(2.0, dmrtc, dmdrvi_ssize_t, _read,
    (dmdrvi_context_t ctx, void* handle, void* buffer, size_t size, dmdrvi_offset_t offset))
{
    (void)ctx; (void)handle; (void)buffer; (void)size; (void)offset;
    return -ENOTSUP;
}
dmod_dmdrvi_dif_api_declaration(2.0, dmrtc, dmdrvi_ssize_t, _write,
    (dmdrvi_context_t ctx, void* handle, const void* buffer, size_t size, dmdrvi_offset_t offset))
{
    (void)ctx; (void)handle; (void)buffer; (void)size; (void)offset;
    return -ENOTSUP;
}
dmod_dmdrvi_dif_api_declaration(2.0, dmrtc, int, _ioctl,
    (dmdrvi_context_t context, void* handle, int command, void* arg))
{
    dmrtc_t ctx = (dmrtc_t)context; struct rtc_file* file = handle;
    if (!dmrtc_is_valid(ctx) || file == NULL || file->owner != ctx) return -EBADF;
    return dmrtc_device_ioctl(ctx, file->flags, command, arg);
}
dmod_dmdrvi_dif_api_declaration(2.0, dmrtc, int, _flush,
    (dmdrvi_context_t context, void* handle))
{
    struct rtc_file* file = handle;
    return dmrtc_is_valid((dmrtc_t)context) && file != NULL &&
        file->owner == (dmrtc_t)context ? 0 : -EBADF;
}
dmod_dmdrvi_dif_api_declaration(2.0, dmrtc, int, _stat,
    (dmdrvi_context_t ctx, const char* path, dmdrvi_stat_t* stat))
{
    (void)path;
    if (!dmrtc_is_valid((dmrtc_t)ctx) || stat == NULL) return -EINVAL;
    stat->size = 0; stat->mode = 0020000 | 0666;
    return 0;
}
