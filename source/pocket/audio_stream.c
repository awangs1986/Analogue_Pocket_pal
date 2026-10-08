/* SPDX-License-Identifier: GPL-3.0-only
 * Cooperative Ogg/Vorbis music decoder. The FPGA receives PCM, not Ogg.
 * Built against SDLPAL's existing BSD-licensed libogg/libvorbis sources.
 */
#include "audio_stream.h"
#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <stdlib.h>
#include <string.h>

enum { INIT_SYNC = 1, INIT_STREAM = 2, INIT_INFO = 4,
       INIT_DSP = 8, INIT_BLOCK = 16 };

struct ppa_stream {
    FILE *file;
    ogg_sync_state sync;
    ogg_stream_state ogg;
    vorbis_info info;
    vorbis_comment comment;
    vorbis_dsp_state dsp;
    vorbis_block block;
    unsigned initialized, headers, header_bytes, packet_bytes;
    int serial, eos, loop;
    ppa_state state;
    const char *error;
    uint64_t pass_frames;
    float previous[2], current[2];
    unsigned phase;
    int have_previous, have_current, final_interval;
    int16_t ring[PPA_RING_FRAMES * 2];
    size_t read_pos, write_pos, queued;
    ppa_stats stats;
};

static void codec_clear(ppa_stream *s)
{
    if (s->initialized & INIT_BLOCK) vorbis_block_clear(&s->block);
    if (s->initialized & INIT_DSP) vorbis_dsp_clear(&s->dsp);
    if (s->initialized & INIT_STREAM) ogg_stream_clear(&s->ogg);
    if (s->initialized & INIT_INFO) {
        vorbis_comment_clear(&s->comment);
        vorbis_info_clear(&s->info);
    }
    if (s->initialized & INIT_SYNC) ogg_sync_clear(&s->sync);
    s->initialized = 0;
}

static int fail(ppa_stream *s, const char *message)
{
    s->state = PPA_ERROR;
    s->error = message;
    return -2;
}

static int codec_begin(ppa_stream *s)
{
    codec_clear(s);
    s->headers = s->header_bytes = s->packet_bytes = 0;
    s->eos = 0;
    s->pass_frames = 0;
    s->phase = 0;
    s->have_previous = s->have_current = s->final_interval = 0;
    if (ogg_sync_init(&s->sync)) return fail(s, "Ogg sync allocation failed");
    s->initialized = INIT_SYNC;
    vorbis_info_init(&s->info);
    vorbis_comment_init(&s->comment);
    s->initialized |= INIT_INFO;
    s->state = PPA_HEADERS;
    return 0;
}

ppa_stream *ppa_create(void)
{
    return (ppa_stream *)calloc(1, sizeof(ppa_stream));
}

void ppa_stop(ppa_stream *s)
{
    if (!s) return;
    codec_clear(s);
    if (s->file) fclose(s->file);
    s->file = NULL;
    s->state = PPA_STOPPED;
    s->error = NULL;
    s->read_pos = s->write_pos = s->queued = 0;
    s->have_previous = s->have_current = 0;
}

void ppa_destroy(ppa_stream *s)
{
    if (s) { ppa_stop(s); free(s); }
}

int ppa_start(ppa_stream *s, FILE *file, int loop)
{
    if (!s) { if (file) fclose(file); return -1; }
    ppa_stop(s);
    memset(&s->stats, 0, sizeof(s->stats));
    s->file = file;
    s->loop = !!loop;
    if (!file) return fail(s, "Ogg file could not be opened");
    return codec_begin(s);
}

void ppa_set_loop(ppa_stream *s, int loop) { if (s) s->loop = !!loop; }

/* Returns 1 for one source frame, 0 when the work budget is exhausted,
 * -1 at clean EOS, -2 on error. No recursive reads on the idle-hook path. */
static int source_frame(ppa_stream *s, float stereo[2], unsigned *budget)
{
    for (;;) {
        ogg_packet packet;
        ogg_page page;
        int result;
        if (s->state == PPA_PLAYING) {
            float **pcm;
            result = vorbis_synthesis_pcmout(&s->dsp, &pcm);
            if (result > 0) {
                stereo[0] = pcm[0][0];
                stereo[1] = s->info.channels == 2 ? pcm[1][0] : pcm[0][0];
                vorbis_synthesis_read(&s->dsp, 1);
                ++s->pass_frames;
                ++s->stats.source_frames;
                return 1;
            }
            if (s->eos) return -1;
        }
        if (!*budget) return 0;
        --*budget;
        ++s->stats.decode_steps;
        if (s->initialized & INIT_STREAM) {
            result = ogg_stream_packetout(&s->ogg, &packet);
            if (result < 0) return fail(s, "Missing or corrupt Ogg packet");
            if (result > 0) {
                if (packet.bytes < 0 || (unsigned long)packet.bytes > PPA_PACKET_LIMIT)
                    return fail(s, "Ogg packet exceeds 256 KiB limit");
                s->packet_bytes = 0;
                if (s->headers < 3) {
                    s->header_bytes += (unsigned)packet.bytes;
                    if (s->header_bytes > PPA_PACKET_LIMIT)
                        return fail(s, "Vorbis headers exceed 256 KiB limit");
                    if (vorbis_synthesis_headerin(&s->info, &s->comment, &packet))
                        return fail(s, "Invalid Vorbis headers");
                    if (++s->headers == 1) {
                        if (s->info.channels < 1 || s->info.channels > 2)
                            return fail(s, "Only mono or stereo Vorbis is supported");
                        if (s->info.rate < 8000 || s->info.rate > PPA_OUTPUT_RATE)
                            return fail(s, "Vorbis rate must be 8000 through 48000 Hz");
                        s->stats.source_rate = (unsigned)s->info.rate;
                        s->stats.source_channels = (unsigned)s->info.channels;
                    }
                    if (s->headers == 3) {
                        if (vorbis_synthesis_init(&s->dsp, &s->info))
                            return fail(s, "Vorbis decoder initialization failed");
                        s->initialized |= INIT_DSP;
                        if (vorbis_block_init(&s->dsp, &s->block))
                            return fail(s, "Vorbis block initialization failed");
                        s->initialized |= INIT_BLOCK;
                        s->state = PPA_PLAYING;
                    }
                } else {
                    if (vorbis_synthesis(&s->block, &packet) ||
                        vorbis_synthesis_blockin(&s->dsp, &s->block))
                        return fail(s, "Invalid Vorbis audio packet");
                    s->eos = !!packet.e_o_s;
                }
                continue;
            }
        }
        result = ogg_sync_pageout(&s->sync, &page);
        if (result < 0) return fail(s, "Ogg page checksum or framing error");
        if (result > 0) {
            if (!(s->initialized & INIT_STREAM)) {
                if (!ogg_page_bos(&page)) return fail(s, "Missing Ogg beginning page");
                s->serial = ogg_page_serialno(&page);
                if (ogg_stream_init(&s->ogg, s->serial))
                    return fail(s, "Ogg stream allocation failed");
                s->initialized |= INIT_STREAM;
            }
            if (ogg_page_serialno(&page) != s->serial)
                return fail(s, "Multiplexed Ogg streams are unsupported");
            /* Bound accumulation before passing a continued packet to libogg.
             * A page can also contain many complete packets, making this a
             * conservative upper bound rather than an exact packet count. */
            if ((unsigned long)(s->ogg.body_fill - s->ogg.body_returned) +
                (unsigned long)page.body_len >
                PPA_PACKET_LIMIT + 65536u)
                return fail(s, "Ogg packet accumulation exceeds limit");
            if (ogg_stream_pagein(&s->ogg, &page))
                return fail(s, "Invalid Ogg page sequence");
            continue;
        }
        {
            char *buffer = ogg_sync_buffer(&s->sync, PPA_READ_BYTES);
            size_t bytes;
            if (!buffer) return fail(s, "Ogg read buffer allocation failed");
            bytes = fread(buffer, 1, PPA_READ_BYTES, s->file);
            if (!bytes)
                return fail(s, ferror(s->file) ? "Ogg file read failed" :
                            "Ogg file ended before an EOS packet");
            s->stats.bytes_read += bytes;
            s->packet_bytes += (unsigned)bytes;
            if (s->packet_bytes > PPA_PACKET_LIMIT + 65536u)
                return fail(s, "No bounded Ogg packet found");
            if (ogg_sync_wrote(&s->sync, (long)bytes))
                return fail(s, "Ogg input accounting failed");
        }
    }
}

static int16_t pcm16(float value)
{
    if (value != value) return 0; /* guard malformed NaN without integer UB */
    if (value >= 1.0f) return 32767;
    if (value <= -1.0f) return -32768;
    return (int16_t)(value * 32768.0f);
}

size_t ppa_pump(ppa_stream *s, size_t frame_budget, unsigned step_budget)
{
    size_t produced = 0;
    if (!s || !frame_budget || !step_budget) return 0;
    while (produced < frame_budget && s->queued < PPA_RING_FRAMES &&
           (s->state == PPA_HEADERS || s->state == PPA_PLAYING)) {
        int result;
        if (!s->have_previous) {
            result = source_frame(s, s->previous, &step_budget);
            if (result == -1) fail(s, "Vorbis stream has no audio frames");
            if (result <= 0) break;
            s->have_previous = 1;
        }
        if (!s->have_current) {
            result = source_frame(s, s->current, &step_budget);
            if (!result || result == -2) break;
            if (result == -1) {
                s->current[0] = s->previous[0];
                s->current[1] = s->previous[1];
                s->final_interval = 1;
            }
            s->have_current = 1;
        }
        while (s->phase < PPA_OUTPUT_RATE && produced < frame_budget &&
               s->queued < PPA_RING_FRAMES) {
            const float fraction = (float)s->phase / (float)PPA_OUTPUT_RATE;
            unsigned ch;
            for (ch = 0; ch < 2; ++ch)
                s->ring[s->write_pos * 2 + ch] = pcm16(s->previous[ch] +
                    (s->current[ch] - s->previous[ch]) * fraction);
            s->write_pos = (s->write_pos + 1) % PPA_RING_FRAMES;
            ++s->queued;
            ++produced;
            ++s->stats.output_frames;
            s->phase += (unsigned)s->info.rate;
        }
        if (s->phase < PPA_OUTPUT_RATE) break;
        s->phase -= PPA_OUTPUT_RATE;
        s->previous[0] = s->current[0];
        s->previous[1] = s->current[1];
        s->have_current = 0;
        if (s->final_interval) {
            if (!s->loop) { s->state = PPA_FINISHED; break; }
            if (fseek(s->file, 0, SEEK_SET)) { fail(s, "Ogg loop rewind failed"); break; }
            ++s->stats.loops;
            if (codec_begin(s)) break;
        }
    }
    return produced;
}

size_t ppa_read(ppa_stream *s, int16_t *stereo, size_t frames)
{
    size_t copied = 0;
    if (!s || !stereo) return 0;
    if (frames > s->queued &&
        (s->state == PPA_HEADERS || s->state == PPA_PLAYING))
        ++s->stats.underrun_reads;
    while (copied < frames && s->queued) {
        size_t count = PPA_RING_FRAMES - s->read_pos;
        if (count > s->queued) count = s->queued;
        if (count > frames - copied) count = frames - copied;
        memcpy(stereo + copied * 2, s->ring + s->read_pos * 2,
               count * 2 * sizeof(int16_t));
        s->read_pos = (s->read_pos + count) % PPA_RING_FRAMES;
        s->queued -= count;
        copied += count;
    }
    return copied;
}

size_t ppa_queued(const ppa_stream *s) { return s ? s->queued : 0; }
ppa_state ppa_status(const ppa_stream *s) { return s ? s->state : PPA_STOPPED; }
const char *ppa_error(const ppa_stream *s) { return s ? s->error : "No decoder"; }
const ppa_stats *ppa_get_stats(const ppa_stream *s) { return s ? &s->stats : NULL; }
