/* SPDX-License-Identifier: GPL-3.0-only */
#include "palcommon.h"
#include "palcfg.h"
#include "audio.h"
#include "audio_stream.h"
#include "util.h"
#include <of_caps.h>
#include <assert.h>

CONFIGURATION gConfig;
static int mixer_available = 1, queued, started, blocked, written_total, next_sample;
static int open_broken;
static unsigned timer_us;
static int16_t captured[10000 * 2];
static AUDIOPLAYER sound;

int of_has_feature(uint32_t feature) { return feature == OF_HW_MIXER && mixer_available; }
void of_audio_init(void) { queued = started = 0; }
int of_audio_free(void) { return started ? 63 - queued : 64; }
int of_audio_write(const int16_t *pcm, int frames)
{
    int count = frames > 7 ? 7 : frames; /* exercise short device writes */
    if (blocked) return 0;
    assert(count <= of_audio_free());
    assert(written_total + count < 10000);
    memcpy(captured + written_total * 2, pcm, (size_t)count * 4);
    written_total += count;
    queued += count;
    started = 1;
    return count;
}
unsigned of_time_us(void) { return timer_us += 100; }
void resampler_init(void) {}
static void sound_shutdown(void *p) { (void)p; }
static BOOL sound_play(void *p, INT n, BOOL loop, FLOAT fade)
{ (void)p; (void)n; (void)loop; (void)fade; return TRUE; }
static void sound_fill(void *p, LPBYTE data, INT length)
{
    int i;
    int16_t *pcm = (int16_t *)data;
    (void)p;
    PAL_PocketAudioDrain(); /* must not recursively mix */
    for (i = 0; i < length / 4; ++i) {
        pcm[i * 2] = (int16_t)next_sample;
        pcm[i * 2 + 1] = (int16_t)-next_sample++;
    }
}
LPAUDIOPLAYER SOUND_Init(void)
{
    sound.Shutdown = sound_shutdown;
    sound.Play = sound_play;
    sound.FillBuffer = sound_fill;
    return &sound;
}
char *UTIL_GlobalBuffer(int index) { static char buffer[1024]; (void)index; return buffer; }
char *UTIL_va(char *buffer, int length, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, (size_t)length, fmt, args);
    va_end(args);
    return buffer;
}
FILE *UTIL_OpenFile(LPCSTR path)
{
    FILE *file;
    (void)path;
    if (!open_broken) return NULL;
    file = tmpfile();
    assert(file);
    fputs("broken Ogg fixture", file);
    rewind(file);
    return file;
}

int main(void)
{
    int i, previous;
    gConfig.iMusicVolume = gConfig.iSoundVolume = 100;
    mixer_available = 0;
    assert(AUDIO_OpenDevice() == -3);
    mixer_available = 1;
    assert(AUDIO_OpenDevice() == 0);
    assert(AUDIO_OpenDevice() == -1);
    assert(AUDIO_GetDeviceSpec()->freq == 48000);
    assert(AUDIO_GetDeviceSpec()->channels == 2);
    assert(!AUDIO_PlayCDTrack(-2));
    AUDIO_EnableMusic(FALSE);
    next_sample = 1;
    AUDIO_Lock(); AUDIO_Lock();
    PAL_PocketAudioDrain();
    assert(written_total == 0);
    AUDIO_Unlock(); PAL_PocketAudioPump();
    assert(written_total == 0);
    AUDIO_Unlock();
    for (i = 0; i < 60; ++i) {
        if (i % 5 == 0) {
            previous = written_total;
            blocked = 1;
            PAL_PocketAudioDrain();
            assert(written_total == previous);
            blocked = 0;
        }
        PAL_PocketAudioPump();
        assert(queued <= 63);
        /* Advance simulated DAC consumption independently of partial writes. */
        queued = queued > 19 ? queued - 19 : 0;
    }
    assert(written_total > 1000);
    for (i = 0; i < written_total; ++i) {
        assert(captured[i * 2] == i + 1);
        assert(captured[i * 2 + 1] == -(i + 1));
    }
    assert(PAL_PocketAudioDiagnostics()->output_frames == (uint64_t)written_total);
    gConfig.eCDType = CD_OGG;
    AUDIO_EnableMusic(TRUE);
    assert(!AUDIO_PlayCDTrack(1)); /* missing file must not retain CD identity */
    assert(!AUDIO_PlayCDTrack(-2));
    open_broken = 1;
    assert(AUDIO_PlayCDTrack(1));
    assert(AUDIO_PlayCDTrack(-2)); /* deferred headers are still in progress */
    PAL_PocketAudioPump();
    assert(!AUDIO_PlayCDTrack(-2)); /* deferred decode failure allows fallback */
    AUDIO_CloseDevice();
    previous = written_total;
    PAL_PocketAudioPump(); PAL_PocketAudioDrain();
    assert(written_total == previous);
    AUDIO_CloseDevice();
    puts("adapter: partial/zero writes, reentrancy, lock nesting, capacity and shutdown PASS");
    return 0;
}
