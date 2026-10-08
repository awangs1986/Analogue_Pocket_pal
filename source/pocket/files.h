/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef PAL_POCKET_FILES_H
#define PAL_POCKET_FILES_H
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PAL_POCKET_ASSET_SLOT 4u
#define PAL_POCKET_SAVE_CAPACITY (256u * 1024u)
/* The runtime exposes full capacity for unknown/zero logical save length.
 * Reserve that ambiguous length, so an empty APF slot cannot look like a save. */
#define PAL_POCKET_SAVE_MAX_BYTES (PAL_POCKET_SAVE_CAPACITY - 1u)
#define PAL_POCKET_ARCHIVE_MAX_FILES 4096u
#define PAL_POCKET_ARCHIVE_NAME_SIZE 120u

/* Call before PAL_LoadConfig. Zero means success. Shutdown requires all
 * returned streams to have been closed. Link with --wrap=fopen,--wrap=access.
 * Asset streams are real FILE objects: ordinary stdio and decoder callbacks
 * work unchanged, and multiple handles have independent cursors. */
int PAL_PocketFilesInit(void);
int PAL_PocketFilesInitPath(const char *archive_path); /* also used by host tests */
void PAL_PocketFilesShutdown(void);
FILE *PAL_PocketOpenAsset(const char *name);
FILE *__wrap_fopen(const char *name, const char *mode);
int __wrap_access(const char *name, int mode);

/* Sticky write/close or initialization diagnostic; a missing optional asset
 * or absent read-only save is not a write error. Reset before PAL_SaveGame,
 * inspect afterwards, and display any error to the player. */
const char *PAL_PocketFileError(void);
void PAL_PocketFileClearError(void);
#ifdef __cplusplus
}
#endif
#endif
