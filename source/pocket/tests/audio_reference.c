/* SPDX-License-Identifier: GPL-3.0-only
 * Independent system-libvorbisfile reference, not the streaming adapter.
 */
#include <stdio.h>
#include <vorbis/vorbisfile.h>
int main(int argc, char **argv)
{
    OggVorbis_File file;
    float **pcm;
    long count;
    int section;
    if (argc != 2 || ov_fopen(argv[1], &file)) return 2;
    while ((count = ov_read_float(&file, &pcm, 2048, &section)) > 0) {
        int channels = ov_info(&file, section)->channels;
        long i;
        for (i = 0; i < count; ++i) {
            int c;
            for (c = 0; c < channels; ++c)
                if (fwrite(&pcm[c][i], sizeof(float), 1, stdout) != 1) return 3;
        }
    }
    ov_clear(&file);
    return count < 0;
}
