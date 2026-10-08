/* SPDX-License-Identifier: GPL-3.0-only */
#include "audio_stream.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    ppa_stream *stream;
    FILE *output;
    int16_t pcm[1024 * 2];
    unsigned iterations = 0;
    size_t total = 0;
    int looping;
    if (argc != 4) return 2;
    looping = atoi(argv[3]);
    stream = ppa_create();
    assert(stream);
    assert(ppa_status(stream) == PPA_STOPPED);
    assert(ppa_start(stream, fopen(argv[1], "rb"), looping) == 0);
    assert(ppa_status(stream) == PPA_HEADERS);
    assert(ppa_get_stats(stream)->bytes_read == 0); /* opening does no I/O */
    assert(ppa_pump(stream, 0, 10) == 0);
    assert(ppa_pump(stream, 10, 0) == 0);
    assert(ppa_get_stats(stream)->bytes_read == 0);
    memset(pcm, 0x55, sizeof(pcm));
    assert(ppa_read(stream, pcm, 17) == 0);
    assert((uint16_t)pcm[0] == 0x5555); /* no stale/replayed data */
    output = fopen(argv[2], "wb");
    assert(output);
    while (++iterations < 100000) {
        unsigned steps = ppa_get_stats(stream)->decode_steps;
        uint64_t bytes = ppa_get_stats(stream)->bytes_read;
        size_t emitted = ppa_pump(stream, 317, 7);
        size_t count;
        assert(emitted <= 317);
        assert(ppa_get_stats(stream)->decode_steps - steps <= 7);
        assert(ppa_get_stats(stream)->bytes_read - bytes <= 7 * PPA_READ_BYTES);
        assert(ppa_queued(stream) <= PPA_RING_FRAMES);
        /* Deliberately skip drains to cover full ring/backpressure/wrap. */
        if (iterations % 37 < 32 && ppa_status(stream) != PPA_FINISHED &&
            ppa_status(stream) != PPA_ERROR) continue;
        count = ppa_read(stream, pcm, 1024);
        assert(fwrite(pcm, sizeof(int16_t) * 2, count, output) == count);
        total += count;
        if (looping && ppa_get_stats(stream)->loops >= 2 && total > 30000) break;
        if ((ppa_status(stream) == PPA_FINISHED || ppa_status(stream) == PPA_ERROR) &&
            !ppa_queued(stream)) break;
    }
    assert(iterations < 100000);
    fclose(output);
    printf("state=%d rate=%u channels=%u frames=%zu loops=%u steps=%u bytes=%llu error=%s\n",
           ppa_status(stream), ppa_get_stats(stream)->source_rate,
           ppa_get_stats(stream)->source_channels, total,
           ppa_get_stats(stream)->loops, ppa_get_stats(stream)->decode_steps,
           (unsigned long long)ppa_get_stats(stream)->bytes_read,
           ppa_error(stream) ? ppa_error(stream) : "none");
    {
        int error = ppa_status(stream) == PPA_ERROR;
        ppa_stop(stream);
        assert(ppa_queued(stream) == 0);
        assert(ppa_status(stream) == PPA_STOPPED);
        ppa_destroy(stream);
        return error ? 1 : 0;
    }
}
