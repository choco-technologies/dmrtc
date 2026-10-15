#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmrtc.h"
#include <errno.h>
#include <string.h>
#ifdef DMRTC_MOCK
#include "dmrtc_mock.h"
#include "dmosi.h"
#endif
static dmrtc_t ctx, owned;
static dmrtc_calendar_t saved;
static bool was_set;
static uint64_t started;
void dmod_test_setup(void)
{
    owned = dmrtc_create(NULL);
    ctx = owned != NULL ? owned : dmrtc_default();
    DMOD_TEST_EXPECT_NOT_NULL(ctx);
    was_set = dmrtc_get(ctx, &saved) == 0;
    started = Dmod_GetUptime();
}
/* Restore the previous civil time, accounting for elapsed whole seconds.
 * Bounded: hardware tests take seconds, not years. */
static void restore(void)
{
    if (!was_set) { DMOD_TEST_EXPECT_EQ(dmrtc_invalidate(ctx), 0); return; }
    uint64_t elapsed = (Dmod_GetUptime() - started) / 1000;
    while (elapsed-- != 0) {
        if (++saved.second < 60) continue;
        saved.second = 0;
        if (++saved.minute < 60) continue;
        saved.minute = 0;
        if (++saved.hour < 24) continue;
        saved.hour = 0; ++saved.day;
        if (dmrtc_normalize(&saved) == 0) continue;
        saved.day = 1;
        if (++saved.month > 12) { saved.month = 1; ++saved.year; }
    }
    DMOD_TEST_EXPECT_EQ(dmrtc_set(ctx, &saved), 0);
}
void dmod_test_teardown(void)
{
    restore();
    if (owned != NULL) DMOD_TEST_EXPECT_EQ(dmrtc_destroy(owned), 0);
    owned = NULL; ctx = NULL;
}
DMOD_TEST_STEP(calendar_validation)
{
    dmrtc_calendar_t c = {2000, 1, 1, 0, 0, 0, 0};
    DMOD_TEST_EXPECT_EQ(dmrtc_normalize(&c), 0); DMOD_TEST_EXPECT_EQ(c.weekday, 6);
    c = (dmrtc_calendar_t){2024, 2, 29, 23, 59, 59, 0};
    DMOD_TEST_EXPECT_EQ(dmrtc_normalize(&c), 0); DMOD_TEST_EXPECT_EQ(c.weekday, 4);
    c.year = 2023; DMOD_TEST_EXPECT_EQ(dmrtc_normalize(&c), -EINVAL);
    c.year = 2100; DMOD_TEST_EXPECT_EQ(dmrtc_normalize(&c), -EINVAL);
    c = (dmrtc_calendar_t){2026, 4, 31, 0, 0, 0, 0};
    DMOD_TEST_EXPECT_EQ(dmrtc_normalize(&c), -EINVAL);
    c.day = 30; c.hour = 24; DMOD_TEST_EXPECT_EQ(dmrtc_normalize(&c), -EINVAL);
    c.hour = 0; c.second = 60; DMOD_TEST_EXPECT_EQ(dmrtc_normalize(&c), -EINVAL);
    c.second = 0; c.month = 0; DMOD_TEST_EXPECT_EQ(dmrtc_normalize(&c), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmrtc_normalize(NULL), -EINVAL);
}
DMOD_TEST_STEP(unset_calendar)
{
    dmrtc_calendar_t c; dmrtc_status_t status;
    DMOD_TEST_EXPECT_EQ(dmrtc_invalidate(ctx), 0);
    memset(&c, 0xa5, sizeof(c));
    DMOD_TEST_EXPECT_EQ(dmrtc_get(ctx, &c), -ENODATA);
    DMOD_TEST_EXPECT_EQ(c.year, 0);
    DMOD_TEST_EXPECT_EQ(dmrtc_status(ctx, &status), 0);
    DMOD_TEST_EXPECT_TRUE(status.running); DMOD_TEST_EXPECT_FALSE(status.calendar_set);
}
DMOD_TEST_STEP(set_read_and_reject_invalid)
{
    dmrtc_calendar_t c = {2024, 2, 29, 12, 34, 56, 0}, out;
    DMOD_TEST_EXPECT_EQ(dmrtc_set(ctx, &c), 0);
    DMOD_TEST_EXPECT_EQ(dmrtc_get(ctx, &out), 0);
    DMOD_TEST_EXPECT_EQ(out.year, 2024); DMOD_TEST_EXPECT_EQ(out.month, 2);
    DMOD_TEST_EXPECT_EQ(out.day, 29); DMOD_TEST_EXPECT_EQ(out.hour, 12);
    DMOD_TEST_EXPECT_EQ(out.minute, 34); DMOD_TEST_EXPECT_EQ(out.weekday, 4);
    c.day = 30; DMOD_TEST_EXPECT_EQ(dmrtc_set(ctx, &c), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmrtc_get(ctx, &out), 0); DMOD_TEST_EXPECT_EQ(out.day, 29);
    DMOD_TEST_EXPECT_EQ(dmrtc_get(NULL, &out), -EINVAL); DMOD_TEST_EXPECT_EQ(out.year, 0);
    DMOD_TEST_EXPECT_EQ(dmrtc_get(ctx, NULL), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmrtc_set(ctx, NULL), -EINVAL);
}
DMOD_TEST_STEP(year_2000_is_not_an_uninitialized_calendar)
{
    dmrtc_calendar_t c = {2000, 2, 29, 12, 0, 0, 0}, out;
    DMOD_TEST_EXPECT_EQ(dmrtc_set(ctx, &c), 0);
    DMOD_TEST_EXPECT_EQ(dmrtc_get(ctx, &out), 0);
    DMOD_TEST_EXPECT_EQ(out.year, 2000); DMOD_TEST_EXPECT_EQ(out.day, 29);
    DMOD_TEST_EXPECT_EQ(out.weekday, 2);
}
DMOD_TEST_STEP(single_owner_and_bad_configuration)
{
    DMOD_TEST_EXPECT_NULL(dmrtc_create(NULL));
    dmrtc_config_t bad = DMRTC_CONFIG_DEFAULT;
    bad.backup_register = 19; DMOD_TEST_EXPECT_NULL(dmrtc_create(&bad));
    bad.backup_register = 18; bad.instance = 1; DMOD_TEST_EXPECT_NULL(dmrtc_create(&bad));
    bad.instance = 0; bad.timeout_ms = 0; DMOD_TEST_EXPECT_NULL(dmrtc_create(&bad));
    bad.timeout_ms = 5000; bad.clock_hz = 0; DMOD_TEST_EXPECT_NULL(dmrtc_create(&bad));
    DMOD_TEST_EXPECT_EQ(dmrtc_destroy(NULL), 0);
}
#ifndef DMRTC_MOCK
DMOD_TEST_STEP(vfs_ioctl_and_access_permissions)
{
    void* reader = Dmod_FileOpen("/dev/rtc", "rb");
    void* writer = Dmod_FileOpen("/dev/rtc", "wb");
    DMOD_TEST_EXPECT_NOT_NULL(reader); DMOD_TEST_EXPECT_NOT_NULL(writer);
    dmrtc_status_t status; dmrtc_calendar_t c = {2026, 10, 15, 1, 2, 3, 0};
    if (reader != NULL) {
        DMOD_TEST_EXPECT_EQ(Dmod_Ioctl(reader, DMRTC_IOCTL_STATUS, &status), 0);
        DMOD_TEST_EXPECT_EQ(Dmod_Ioctl(reader, DMRTC_IOCTL_SET, &c), -EACCES);
        DMOD_TEST_EXPECT_EQ(Dmod_Ioctl(reader, DMRTC_IOCTL_INVALIDATE, NULL), -EACCES);
        DMOD_TEST_EXPECT_EQ(Dmod_Ioctl(reader, 12345, NULL), -ENOTTY);
        DMOD_TEST_EXPECT_EQ(dmrtc_destroy(ctx), -EBUSY);
    }
    if (writer != NULL) {
        DMOD_TEST_EXPECT_EQ(Dmod_Ioctl(writer, DMRTC_IOCTL_SET, &c), 0);
        DMOD_TEST_EXPECT_EQ(Dmod_Ioctl(writer, DMRTC_IOCTL_GET, &c), -EACCES);
    }
    if (reader != NULL) {
        DMOD_TEST_EXPECT_EQ(Dmod_Ioctl(reader, DMRTC_IOCTL_GET, &c), 0);
        DMOD_TEST_EXPECT_EQ(c.year, 2026);
        Dmod_FileClose(reader);
    }
    if (writer != NULL) Dmod_FileClose(writer);
}
DMOD_TEST_STEP(leap_day_rollover)
{
    dmrtc_calendar_t c = {2024, 2, 28, 23, 59, 59, 0}, out;
    DMOD_TEST_EXPECT_EQ(dmrtc_set(ctx, &c), 0);
    Dmod_ThreadSleep(1300);
    DMOD_TEST_EXPECT_EQ(dmrtc_get(ctx, &out), 0);
    DMOD_TEST_EXPECT_EQ(out.year, 2024); DMOD_TEST_EXPECT_EQ(out.month, 2);
    DMOD_TEST_EXPECT_EQ(out.day, 29); DMOD_TEST_EXPECT_EQ(out.hour, 0);
    DMOD_TEST_EXPECT_EQ(out.minute, 0); DMOD_TEST_EXPECT_EQ(out.weekday, 4);
}
DMOD_TEST_STEP(year_rollover)
{
    dmrtc_calendar_t c = {2023, 12, 31, 23, 59, 59, 0}, out;
    DMOD_TEST_EXPECT_EQ(dmrtc_set(ctx, &c), 0);
    Dmod_ThreadSleep(1300);
    DMOD_TEST_EXPECT_EQ(dmrtc_get(ctx, &out), 0);
    DMOD_TEST_EXPECT_EQ(out.year, 2024); DMOD_TEST_EXPECT_EQ(out.month, 1);
    DMOD_TEST_EXPECT_EQ(out.day, 1); DMOD_TEST_EXPECT_EQ(out.weekday, 1);
}
#else
static void concurrent_writer(void* arg)
{
    int* result = arg;
    dmrtc_calendar_t c = {2024, 2, 29, 12, 0, 0, 0};
    *result = 0;
    for (unsigned i = 0; i < 500 && *result == 0; ++i) *result = dmrtc_set(ctx, &c);
}
DMOD_TEST_STEP(concurrent_calendar_access)
{
    dmrtc_calendar_t c = {2026, 10, 15, 0, 0, 0, 0}, out;
    DMOD_TEST_EXPECT_EQ(dmrtc_set(ctx, &c), 0);
    int result = -1;
    dmosi_thread_t writer = dmosi_thread_create(concurrent_writer, &result, 1, 16384, "rtc", NULL);
    DMOD_TEST_EXPECT_NOT_NULL(writer);
    for (unsigned i = 0; i < 500; ++i) {
        DMOD_TEST_EXPECT_EQ(dmrtc_get(ctx, &out), 0);
        bool first = out.year == 2026 && out.month == 10 && out.day == 15 && out.hour == 0;
        bool second = out.year == 2024 && out.month == 2 && out.day == 29 && out.hour == 12;
        DMOD_TEST_EXPECT_TRUE(first || second);
    }
    if (writer != NULL) {
        DMOD_TEST_EXPECT_EQ(dmosi_thread_join(writer), 0);
        dmosi_thread_destroy(writer);
    }
    DMOD_TEST_EXPECT_EQ(result, 0);
}
DMOD_TEST_STEP(failed_set_invalidates_calendar)
{
    dmrtc_calendar_t c = {2026, 10, 15, 0, 0, 0, 0}, out;
    DMOD_TEST_EXPECT_EQ(dmrtc_set(ctx, &c), 0);
    dmrtc_port_mock_fail_set(-ETIMEDOUT);
    DMOD_TEST_EXPECT_EQ(dmrtc_set(ctx, &c), -ETIMEDOUT);
    DMOD_TEST_EXPECT_EQ(dmrtc_get(ctx, &out), -ENODATA);
    DMOD_TEST_EXPECT_EQ(out.year, 0);
}
DMOD_TEST_STEP(calendar_survives_recreate_and_source_conflict)
{
    dmrtc_calendar_t c = {2026, 10, 15, 0, 0, 0, 0}, out;
    DMOD_TEST_EXPECT_EQ(dmrtc_set(ctx, &c), 0);
    DMOD_TEST_EXPECT_EQ(dmrtc_destroy(owned), 0); owned = NULL; ctx = NULL;
    dmrtc_config_t conflict = DMRTC_CONFIG_DEFAULT; conflict.clock = DMRTC_CLOCK_LSI;
    DMOD_TEST_EXPECT_NULL(dmrtc_create(&conflict));
    owned = dmrtc_create(NULL); ctx = owned;
    DMOD_TEST_EXPECT_NOT_NULL(ctx);
    DMOD_TEST_EXPECT_EQ(dmrtc_get(ctx, &out), 0);
    DMOD_TEST_EXPECT_EQ(out.year, 2026); DMOD_TEST_EXPECT_EQ(out.day, 15);
}
#endif
