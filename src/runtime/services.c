#include "runtime.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <libkern/OSCacheControl.h>

// These initializers normally open IPC sessions. The native host supplies APIs
// directly, so no session is opened. Unimplemented subsequent APIs still stop
// at the kernel boundary; these are not implementations of the full services.
static void log_init(void) { fputs("host diagnostics: stderr\n", stderr); }
static void oe_init(void) { fputs("host execution environment selected\n", stderr); }
static void aoc_init(void) { fputs("host content: base application only\n", stderr); }
static void err_init(void) { fputs("host error reports: local only\n", stderr); }
static void fs_init(void) { fputs("host filesystem API selected\n", stderr); }
static void pctl_init(void) { fputs("host parental-control IPC disabled\n", stderr); }
static void abort_init(void) { fputs("host abort diagnostics installed\n", stderr); }
static void operation_notifications(int enabled) { (void)enabled; /* Fixed display mode has no transitions. */ }
static unsigned operation_mode(void) { return 0; /* Handheld: fixed 1280 x 720 logical surface. */ }
static unsigned focus_state(void) { return 1; }
static unsigned resolution(int *width, int *height) { *width = 1280; *height = 720; return 0; }
static void performance(unsigned mode, unsigned configuration) { (void)mode; (void)configuration; }
static int pop_notification(unsigned *message) { (void)message; return 0; }
static void fs_allocator(void *allocate, void *release) { (void)allocate; (void)release; /* Host-owned handles use host allocation. */ }
static void ro_init(void) { fputs("host module registry: main, subsdk0, sdk\n", stderr); }
static unsigned time_init(void) { tzset(); return 0; }
static void flush_data(const void *data, size_t size) { sys_dcache_flush((void *)data, size); }
static unsigned current_time(int64_t *out) { *out = time(NULL); return 0; }
typedef struct { int16_t year; int8_t month, day, hour, minute, second, padding; } Calendar;
typedef struct { uint32_t weekday, yearday; char zone[8]; uint32_t dst; int32_t offset; } CalendarInfo;
static unsigned calendar_time(Calendar *out, CalendarInfo *info, const int64_t *input) {
    time_t value = *input; struct tm tm;
    if (!localtime_r(&value, &tm)) return 0xfa01;
    *out = (Calendar){tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, 0};
    if (info) { memset(info, 0, sizeof *info); info->weekday = tm.tm_wday; info->yearday = tm.tm_yday;
        info->dst = tm.tm_isdst > 0; info->offset = tm.tm_gmtoff; strlcpy(info->zone, tm.tm_zone ? tm.tm_zone : "UTC", sizeof info->zone); }
    return 0;
}
void *hy_service(const char *name) {
    void *graphics = hy_graphics_service(name);
    if (graphics) return graphics;
    void *audio = hy_audio_service(name);
    if (audio) return audio;
    void *input = hy_input_service(name);
    if (input) return input;
    void *fs = hy_filesystem_service(name);
    if (fs) return fs;
    static const struct { const char *name; void *fn; } functions[] = {
        {"nndiagStartup", (void *)log_init},
        {"_ZN2nn2oe10InitializeEv", (void *)oe_init},
        {"_ZN2nn3aoc10InitializeEv", (void *)aoc_init},
        {"_ZN2nn3err26InitializeContextWriterApiEv", (void *)err_init},
        {"_ZN2nn2fs24InitializeForApplicationEv", (void *)fs_init},
        {"_ZN2nn4pctl10InitializeEv", (void *)pctl_init},
        {"_ZN2nn4diag34InitializeApplicationAbortObserverEv", (void *)abort_init},
        {"_ZN2nn2oe42SetOperationModeChangedNotificationEnabledEb", (void *)operation_notifications},
        {"_ZN2nn2oe16GetOperationModeEv", (void *)operation_mode},
        {"_ZN2nn2oe20GetCurrentFocusStateEv", (void *)focus_state},
        {"_ZN2nn2oe27GetDefaultDisplayResolutionEPiS1_", (void *)resolution},
        {"_ZN2nn2oe27SetPerformanceConfigurationENS0_15PerformanceModeEi", (void *)performance},
        {"_ZN2nn2oe25TryPopNotificationMessageEPj", (void *)pop_notification},
        {"_ZN2nn2fs12SetAllocatorEPFPvmEPFvS1_mE", (void *)fs_allocator},
        {"_ZN2nn2ro10InitializeEv", (void *)ro_init},
        {"_ZN2nn4time10InitializeEv", (void *)time_init},
        {"_ZN2nn2os14FlushDataCacheEPKvm", (void *)flush_data},
        {"_ZN2nn4time23StandardUserSystemClock14GetCurrentTimeEPNS0_9PosixTimeE", (void *)current_time},
        {"_ZN2nn4time14ToCalendarTimeEPNS0_12CalendarTimeEPNS0_22CalendarAdditionalInfoERKNS0_9PosixTimeE", (void *)calendar_time},
    };
    for (size_t i = 0; i < sizeof functions / sizeof functions[0]; i++)
        if (!strcmp(name, functions[i].name)) return (void *)functions[i].fn;
    return NULL;
}
