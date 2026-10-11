/* SPDX-License-Identifier: GPL-3.0-only
 * Weak no-op stand-ins for the Pocket log/timing/prefetch hooks so the host
 * unit tests can link individual backend files. */
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#define WEAK __attribute__((weak))
WEAK void PAL_PocketLog(const char *fmt, ...) { (void)fmt; }
WEAK void PAL_PocketLogRaw(const char *s) { (void)s; }
WEAK void PAL_PocketLogHeartbeat(const char *where) { (void)where; }
WEAK void PAL_PocketLogBootDone(void) {}
WEAK const char *PAL_PocketLogLast(void) { return ""; }
WEAK void PAL_PocketNotePresent(void) {}
WEAK void PAL_PocketStallCheck(void) {}
WEAK void PAL_PocketPrefetchPump(void) {}
WEAK void PAL_PocketFilesQuiesce(void) {}
WEAK void PAL_PocketVideoRestorePalette(void) {}
WEAK uint32_t PAL_PocketTimeUs(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000000u + t.tv_nsec / 1000u);
}
