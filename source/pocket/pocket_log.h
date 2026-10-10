/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Boot/debug log: Saves/<platform>/common/PAL_debug.log (data slot 15). */
#ifndef POCKET_LOG_H
#define POCKET_LOG_H
#ifdef __cplusplus
extern "C" {
#endif
void PAL_PocketLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void PAL_PocketLogRaw(const char *text);
void PAL_PocketLogHeartbeat(const char *where);
void PAL_PocketLogBootDone(void);
const char *PAL_PocketLogLast(void);
#ifdef __cplusplus
}
#endif
#endif
