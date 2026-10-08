/* SPDX-License-Identifier: GPL-3.0-only
 * Synthetic fixtures only. Run through test_pack_assets.py. */
#define _GNU_SOURCE
#include "../files.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fail_open, fail_close, fail_write;
static ssize_t broken_write(void *c, const char *p, size_t n)
{
    (void)c; (void)p;
    if (fail_write) { errno = EIO; return 0; }
    return (ssize_t)n;
}
static int broken_close(void *c)
{
    (void)c;
    if (fail_close) { errno = EIO; return -1; }
    return 0;
}
FILE *__real_fopen(const char *path, const char *mode)
{
    if (!strcmp(path, "PAL_5.sav") && mode[0] == 'w') {
        if (fail_open) { errno = EACCES; return NULL; }
        if (fail_close || fail_write) {
            cookie_io_functions_t io = { NULL, broken_write, NULL, broken_close };
            FILE *f = fopencookie(NULL, "wb", io);
            assert(f);
            setvbuf(f, NULL, _IONBF, 0);
            return f;
        }
    }
    return fopen(path, mode);
}
static void check_file(FILE *f, const char *expected)
{
    char data[128] = {0};
    assert(f);
    assert(fread(data, 1, sizeof(data), f) == strlen(expected));
    assert(!strcmp(data, expected));
    assert(feof(f));
    assert(!ferror(f));
    assert(!fclose(f));
}
static void check_sticky_failure(void)
{
    FILE *f;
    PAL_PocketFileClearError();
    f = __wrap_fopen("5.rpg", "wb");
    assert(f); /* The backing file is only opened on close. */
    assert(fwrite("new", 1, 3, f) == 3);
    assert(fclose(f) != 0);
    assert(PAL_PocketFileError());
    PAL_PocketFileClearError();
}
int main(int argc, char **argv)
{
    FILE *a, *b;
    char data[128];
    assert(argc == 2 || argc == 3);
    if (argc == 3) {
        assert(PAL_PocketFilesInitPath(argv[1]) != 0);
        assert(PAL_PocketFileError());
        PAL_PocketFilesShutdown();
        return 0;
    }
    assert(!PAL_PocketFilesInitPath(argv[1]));
    assert(!PAL_PocketFileError());
    /* Reproduce pinned open_nv_fd's zero-length read fallback: a full-size
     * zero-filled nonvolatile slot must not be exposed as an existing game. */
    a = fopen("PAL_1.sav", "wb");
    assert(a && !fseek(a, PAL_POCKET_SAVE_CAPACITY - 1, SEEK_SET));
    assert(fputc(0, a) == 0 && !fclose(a));
    a = fopen("PAL.cfg", "wb");
    assert(a && !fseek(a, PAL_POCKET_SAVE_CAPACITY - 1, SEEK_SET));
    assert(fputc(0, a) == 0 && !fclose(a));
    a = __wrap_fopen("./DaTa.MKF", "rb");
    b = __wrap_fopen("/OGG\\001.OGG", "rb");
    assert(a && b);
    assert(fread(data, 1, 3, a) == 3 && !memcmp(data, "012", 3));
    assert(fread(data, 1, 4, b) == 4 && !memcmp(data, "OGG-", 4));
    assert(fread(data, 1, 2, a) == 2 && !memcmp(data, "34", 2));
    assert(!fseek(b, -4, SEEK_END));
    assert(ftell(b) == 5);
    assert(fread(data, 1, 4, b) == 4 && !memcmp(data, "test", 4));
    assert(!fseek(a, 0, SEEK_END) && ftell(a) == 10);
    assert(fgetc(a) == EOF && feof(a));
    rewind(a);
    assert(!feof(a) && fgetc(a) == '0');
    assert(fseek(a, -1, SEEK_SET) != 0);
    assert(fseek(a, 1, SEEK_END) != 0);
    assert(fseek(a, LONG_MAX, SEEK_CUR) != 0);
    assert(!fclose(a) && !fclose(b));
    a = __wrap_fopen("empty.dat", "rb");
    assert(a && fgetc(a) == EOF && feof(a) && !fclose(a));
    assert(__wrap_access("DATA.MKF", F_OK) == 0);
    assert(__wrap_access("data.mkf", R_OK) == 0);
    assert(__wrap_access("data.mkf", W_OK) == -1);
    assert(__wrap_access("data.mkf", X_OK) == -1);
    assert(!__wrap_fopen("../data.mkf", "rb"));
    assert(!__wrap_fopen("C:data.mkf", "rb"));
    assert(!__wrap_fopen("missing.ogg", "rb"));
    assert(!__wrap_fopen("1.rpg", "rb"));
    assert(__wrap_access("1.rpg", F_OK) != 0);
    assert(!__wrap_fopen("2.rpg", "rb"));
    assert(!PAL_PocketFileError());
    check_file(__wrap_fopen("sdlpal.cfg", "r"), "Music=OGG\n");
    assert(!__wrap_fopen("1.rpg", "r+b") && PAL_PocketFileError());
    PAL_PocketFileClearError();
    assert(!__wrap_fopen("1.rpg", "ab") && PAL_PocketFileError());
    PAL_PocketFileClearError();

    a = __wrap_fopen("1.rpg", "wb");
    assert(a && fwrite("save-v1", 1, 7, a) == 7 && !fclose(a));
    assert(!PAL_PocketFileError());
    check_file(__wrap_fopen("./1.RPG", "rb"), "save-v1");
    assert(!__wrap_access("1.rpg", R_OK | W_OK));
    a = __wrap_fopen("1.rpg", "r+b");
    assert(a && !fseek(a, 5, SEEK_SET) && fwrite("v2", 1, 2, a) == 2 && !fclose(a));
    check_file(__wrap_fopen("1.rpg", "rb"), "save-v2");
    a = __wrap_fopen("1.rpg", "ab");
    assert(a && !fseek(a, 0, SEEK_SET) && fwrite("!", 1, 1, a) == 1 && !fclose(a));
    check_file(__wrap_fopen("1.rpg", "rb"), "save-v2!");
    a = __wrap_fopen("2.rpg", "wb");
    assert(a && !fseek(a, 3, SEEK_SET) && fwrite("x", 1, 1, a) == 1 && !fclose(a));
    a = __wrap_fopen("2.rpg", "rb");
    assert(a && fread(data, 1, 4, a) == 4 && !memcmp(data, "\0\0\0x", 4) && !fclose(a));

    /* An overflow must preserve the previous backing save, not truncate it. */
    a = __wrap_fopen("1.rpg", "wb");
    assert(a && !fseek(a, PAL_POCKET_SAVE_MAX_BYTES, SEEK_SET));
    fwrite("overflow", 1, 8, a);
    assert(ferror(a) && PAL_PocketFileError());
    assert(fclose(a) != 0);
    PAL_PocketFileClearError();
    check_file(__wrap_fopen("1.rpg", "rb"), "save-v2!");
    assert(!__wrap_fopen("6.rpg", "wb") && PAL_PocketFileError());
    PAL_PocketFileClearError();
    a = __wrap_fopen("3.rpg", "wb");
    assert(a);
    assert(!__wrap_fopen("3.rpg", "wb") && PAL_PocketFileError());
    assert(!fclose(a));
    PAL_PocketFileClearError();

    a = __wrap_fopen("sdlpal.cfg", "w");
    assert(a && fprintf(a, "Volume=%d\n", 75) == 10 && !fclose(a));
    check_file(__wrap_fopen("sdlpal.cfg", "r"), "Volume=75\n");
    fail_open = 1; check_sticky_failure(); fail_open = 0;
    fail_write = 1; check_sticky_failure(); fail_write = 0;
    fail_close = 1; check_sticky_failure(); fail_close = 0;
    PAL_PocketFilesShutdown();
    assert(!PAL_PocketFileError());
    assert(!PAL_PocketFilesInitPath(argv[1]));
    check_file(__wrap_fopen("1.rpg", "rb"), "save-v2!");
    PAL_PocketFilesShutdown();
    puts("files: independent cursors, bounded streams, save roundtrip and failure reporting passed");
    return 0;
}
