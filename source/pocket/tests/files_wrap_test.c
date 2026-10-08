/* SPDX-License-Identifier: GPL-3.0-only */
#include "../files.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    char buffer[16] = {0};
    FILE *file;
    assert(argc == 2);
    assert(!PAL_PocketFilesInitPath(argv[1]));
    assert(!access("DATA.MKF", R_OK));
    file = fopen("DATA.MKF", "rb");
    assert(file && fread(buffer, 1, sizeof(buffer), file) == 10);
    assert(!memcmp(buffer, "0123456789", 10) && !fclose(file));
    file = fopen("4.rpg", "wb");
    assert(file && fwrite("link-test", 1, 9, file) == 9 && !fclose(file));
    assert(!PAL_PocketFileError());
    file = fopen("4.rpg", "rb");
    assert(file && fread(buffer, 1, sizeof(buffer), file) == 9);
    assert(!memcmp(buffer, "link-test", 9) && !fclose(file));
    PAL_PocketFilesShutdown();
    return 0;
}
