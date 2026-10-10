/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Lightweight boot/debug log for the Pocket port.
 *
 * Storage: nonvolatile data slot 15 ("PAL_debug.log", 256 KiB CRAM0 save
 * window at bridge 0x20240000). Every line rewrites the whole RAM copy into
 * the slot through the OS save path and commits the size, so the log is
 * complete in CRAM even if the CPU hangs right after. The APF host writes
 * the slot to SD when the core is quit from the Pocket menu
 * (Saves/<platform>/common/PAL_debug.log).
 *
 * Screen: until the game reaches the title (PAL_PocketLogBootDone), every
 * line is also printed to the openfpgaOS terminal and the terminal is shown
 * as a white overlay above the framebuffer, so a hang shows the last step. */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "of.h"
#include "pocket_log.h"

#define LOG_CAP (240u * 1024u)
#define LOG_NAME "PAL_debug.log"
#define LOG_SLOT 15u

static char g_buf[LOG_CAP];
static uint32_t g_len, g_lines;
static int g_registered, g_boot_done, g_busy, g_full;
static char g_last[160];
extern FILE *__real_fopen(const char *, const char *);

static void commit(void)
{
    FILE *f = __real_fopen(LOG_NAME, "wb");
    if (!f) return;
    fwrite(g_buf, 1, g_len, f);
    fclose(f);
}

void PAL_PocketLogRaw(const char *text)
{
    size_t n;
    uint32_t ms;
    char line[400];
    if (!text || g_busy) return;
    g_busy = 1;
    if (!g_registered) { of_file_slot_register(LOG_SLOT, LOG_NAME); g_registered = 1; }
    ms = of_time_ms();
    snprintf(line, sizeof(line), "%6lu.%03lu #%lu %s", (unsigned long)(ms / 1000),
             (unsigned long)(ms % 1000), (unsigned long)g_lines++, text);
    n = strlen(line);
    if (n && line[n - 1] != '\n' && n + 1 < sizeof(line)) { line[n++] = '\n'; line[n] = 0; }
    snprintf(g_last, sizeof(g_last), "%s", line);
    if (!g_full) {
        if (g_len + n + 32 > LOG_CAP) {
            static const char tail[] = "[log full]\n";
            memcpy(g_buf + g_len, tail, sizeof(tail) - 1);
            g_len += sizeof(tail) - 1;
            g_full = 1;
        } else {
            memcpy(g_buf + g_len, line, n);
            g_len += (uint32_t)n;
        }
        commit();
    }
    if (!g_boot_done) {
        fputs(line, stdout);
        fflush(stdout);
        of_video_set_display_mode(OF_DISPLAY_OVERLAY);
    }
    g_busy = 0;
}

void PAL_PocketLog(const char *fmt, ...)
{
    char text[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    PAL_PocketLogRaw(text);
}

void PAL_PocketLogHeartbeat(const char *where)
{
    static uint32_t last, count;
    uint32_t now = of_time_ms();
    ++count;
    if (now - last < 5000u) return;
    last = now;
    PAL_PocketLog("heartbeat %s calls=%lu brk=%p", where, (unsigned long)count, sbrk(0));
}

void PAL_PocketLogBootDone(void)
{
    if (g_boot_done) return;
    PAL_PocketLogRaw("boot done: terminal overlay off");
    g_boot_done = 1;
    of_video_set_display_mode(OF_DISPLAY_FRAMEBUFFER);
}

const char *PAL_PocketLogLast(void) { return g_last; }
