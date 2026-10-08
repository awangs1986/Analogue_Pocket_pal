/* SPDX-License-Identifier: GPL-3.0-only
 * SDLPAL/openfpgaOS adapter: cooperative CPU Vorbis + existing WAV/VOC SFX.
 * Replaces sdlpal/audio.c and oggplay.c for the Pocket target only.
 */
#include "palcommon.h"
#include "global.h"
#include "palcfg.h"
#include "audio.h"
#include "players.h"
#include "resampler.h"
#include "util.h"
#include "audio_stream.h"
#include <of_audio.h>
#include <of_caps.h>
#include <of_timer.h>
#include <limits.h>

enum { MIX_FRAMES = 256, QUEUE_FRAMES = 2048, PUMP_FRAMES = 4096,
       PUMP_STEPS = 64, DRAIN_CHUNKS = 8 };

AUDIODEVICE gAudioDevice;
static ppa_stream *music;
static int current_track = -1, is_cd, lock_depth, pumping, draining;
static int capacity, pending_frames, pending_offset;
static int16_t mix_buffer[MIX_FRAMES * 2], sfx_buffer[MIX_FRAMES * 2];
static unsigned fade_frames, fade_position;
static uint32_t last_pump_us, last_report_us;
static int have_pump_time, hardware_started, was_starved, error_reported;
static pal_pocket_audio_diagnostics diagnostics;

static int clamp_volume(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

static void set_volumes(void)
{
    gConfig.iMusicVolume = clamp_volume(gConfig.iMusicVolume);
    gConfig.iSoundVolume = clamp_volume(gConfig.iSoundVolume);
    gAudioDevice.iMusicVolume = gConfig.iMusicVolume * SDL_MIX_MAXVOLUME / 100;
    gAudioDevice.iSoundVolume = gConfig.iSoundVolume * SDL_MIX_MAXVOLUME / 100;
}

const pal_pocket_audio_diagnostics *PAL_PocketAudioDiagnostics(void)
{
    return &diagnostics;
}

INT AUDIO_OpenDevice(VOID)
{
    if (gAudioDevice.fOpened) return -1;
    if (!of_has_feature(OF_HW_MIXER)) {
        fprintf(stderr, "Pocket audio: OS mixer service unavailable\n");
        return -3;
    }
    music = ppa_create();
    if (!music) return -3;
    memset(&gAudioDevice, 0, sizeof(gAudioDevice));
    memset(&diagnostics, 0, sizeof(diagnostics));
    pending_frames = pending_offset = lock_depth = pumping = draining = 0;
    current_track = -1;
    is_cd = have_pump_time = hardware_started = was_starved = error_reported = 0;
    fade_frames = fade_position = 0;
    gConfig.iSampleRate = PPA_OUTPUT_RATE;
    gConfig.iAudioChannels = 2;
    gConfig.eMusicType = MUSIC_OGG;
    if (gConfig.eCDType != CD_NONE) gConfig.eCDType = CD_OGG;
    gAudioDevice.spec.freq = PPA_OUTPUT_RATE;
    gAudioDevice.spec.channels = 2;
    gAudioDevice.spec.format = AUDIO_S16SYS;
    gAudioDevice.spec.samples = MIX_FRAMES;
    gAudioDevice.fMusicEnabled = gAudioDevice.fSoundEnabled = TRUE;
    set_volumes();
    resampler_init();
    gAudioDevice.pSoundPlayer = SOUND_Init();
    gAudioDevice.pSoundBuffer = sfx_buffer;
    of_audio_init();
    capacity = of_audio_free();
    if (capacity <= 1) {
        if (gAudioDevice.pSoundPlayer)
            gAudioDevice.pSoundPlayer->Shutdown(gAudioDevice.pSoundPlayer);
        gAudioDevice.pSoundPlayer = NULL;
        gAudioDevice.pSoundBuffer = NULL;
        ppa_destroy(music);
        music = NULL;
        return -3;
    }
    gAudioDevice.fOpened = TRUE;
    last_report_us = of_time_us();
    fprintf(stderr, "Pocket audio: CPU Vorbis -> 48000 Hz stereo PCM, ring=%d, queue<=%d frames\n",
            capacity, QUEUE_FRAMES);
    return 0;
}

VOID AUDIO_CloseDevice(VOID)
{
    if (!gAudioDevice.fOpened) return;
    gAudioDevice.fOpened = FALSE;
    of_audio_init(); /* stop/reset this application's PCM stream */
    ppa_destroy(music);
    music = NULL;
    if (gAudioDevice.pSoundPlayer)
        gAudioDevice.pSoundPlayer->Shutdown(gAudioDevice.pSoundPlayer);
    gAudioDevice.pSoundPlayer = NULL;
    gAudioDevice.pSoundBuffer = NULL;
    pending_frames = pending_offset = 0;
}

/* Does not read files, decode Vorbis, or wait for hardware. Partial device
 * writes retain the exact unsubmitted PCM instead of skipping/duplicating it.
 * SOUND_FillBuffer only consumes already loaded samples. */
void PAL_PocketAudioDrain(void)
{
    unsigned chunks;
    if (!gAudioDevice.fOpened || lock_depth || draining) return;
    draining = 1;
    for (chunks = 0; chunks < DRAIN_CHUNKS; ++chunks) {
        int free_frames = of_audio_free();
        int queued = capacity - 1 - free_frames;
        int target = QUEUE_FRAMES < capacity - 1 ? QUEUE_FRAMES : capacity - 1;
        int room, written;
        if (queued < 0) queued = 0;
        if (hardware_started && queued <= 4) {
            if (!was_starved) ++diagnostics.hardware_starvations;
            was_starved = 1;
        } else was_starved = 0;
        room = target - queued;
        if (room > free_frames) room = free_frames;
        if (room <= 0) break;
        if (!pending_frames) {
            int frames = room < MIX_FRAMES ? room : MIX_FRAMES;
            int i;
            size_t have = 0;
            memset(mix_buffer, 0, (size_t)frames * 2 * sizeof(int16_t));
            memset(sfx_buffer, 0, (size_t)frames * 2 * sizeof(int16_t));
            if (gAudioDevice.fMusicEnabled) {
                have = ppa_read(music, mix_buffer, (size_t)frames);
                if (have < (size_t)frames &&
                    (ppa_status(music) == PPA_PLAYING || ppa_status(music) == PPA_HEADERS))
                    diagnostics.silence_frames += frames - have;
            }
            if (gAudioDevice.fSoundEnabled && gAudioDevice.pSoundPlayer)
                gAudioDevice.pSoundPlayer->FillBuffer(gAudioDevice.pSoundPlayer,
                    (LPBYTE)sfx_buffer, frames * 2 * (int)sizeof(int16_t));
            for (i = 0; i < frames; ++i) {
                int ch, volume = gAudioDevice.iMusicVolume;
                if (fade_position < fade_frames && (size_t)i < have) {
                    volume = (int)((uint64_t)volume * fade_position / fade_frames);
                    ++fade_position;
                }
                for (ch = 0; ch < 2; ++ch) {
                    int index = i * 2 + ch;
                    int value = mix_buffer[index] * volume / SDL_MIX_MAXVOLUME +
                        sfx_buffer[index] * gAudioDevice.iSoundVolume / SDL_MIX_MAXVOLUME;
                    mix_buffer[index] = (int16_t)(value > 32767 ? 32767 :
                                                        value < -32768 ? -32768 : value);
                }
            }
            pending_frames = frames;
            pending_offset = 0;
        }
        if (room > pending_frames) room = pending_frames;
        written = of_audio_write(mix_buffer + pending_offset * 2, room);
        if (written <= 0) break;
        if (written > room) written = room; /* defensive service boundary */
        hardware_started = 1;
        pending_frames -= written;
        pending_offset += written;
        diagnostics.output_frames += (unsigned)written;
    }
    draining = 0;
}

void PAL_PocketAudioPump(void)
{
    uint32_t start, elapsed, gap;
    const ppa_stats *stats;
    if (!gAudioDevice.fOpened || lock_depth || pumping) return;
    pumping = 1;
    start = of_time_us();
    if (have_pump_time) {
        gap = start - last_pump_us;
        if (gap > diagnostics.max_pump_gap_us) diagnostics.max_pump_gap_us = gap;
    }
    last_pump_us = start;
    have_pump_time = 1;
    PAL_PocketAudioDrain();
    start = of_time_us();
    if (gAudioDevice.fMusicEnabled) ppa_pump(music, PUMP_FRAMES, PUMP_STEPS);
    elapsed = of_time_us() - start;
    diagnostics.decode_us += elapsed;
    if (elapsed > diagnostics.max_decode_us) diagnostics.max_decode_us = elapsed;
    ++diagnostics.pump_calls;
    PAL_PocketAudioDrain();
    stats = ppa_get_stats(music);
    diagnostics.software_underruns = stats->underrun_reads;
    diagnostics.source_rate = stats->source_rate;
    diagnostics.source_channels = stats->source_channels;
    diagnostics.pcm_queued = (uint32_t)ppa_queued(music);
    if (ppa_status(music) == PPA_ERROR && !error_reported) {
        fprintf(stderr, "Pocket Ogg %d: %s\n", current_track, ppa_error(music));
        error_reported = 1;
        current_track = -1;
        is_cd = 0;
    }
    if ((uint32_t)(of_time_us() - last_report_us) >= 5000000u && current_track > 0) {
        last_report_us = of_time_us();
        fprintf(stderr, "Pocket audio: track=%d src=%luHz/%luch queued=%lu decode_max=%luus gap_max=%luus underruns=%lu/%lu\n",
                current_track, (unsigned long)diagnostics.source_rate,
                (unsigned long)diagnostics.source_channels,
                (unsigned long)diagnostics.pcm_queued, (unsigned long)diagnostics.max_decode_us,
                (unsigned long)diagnostics.max_pump_gap_us,
                (unsigned long)diagnostics.software_underruns,
                (unsigned long)diagnostics.hardware_starvations);
    }
    pumping = 0;
}

static BOOL play_track(INT track, BOOL loop, FLOAT fade, int cd)
{
    FILE *file;
    if (!gAudioDevice.fOpened) return FALSE;
    if (track <= 0) {
        ppa_stop(music);
        current_track = -1;
        is_cd = 0;
        return TRUE;
    }
    if (track == current_track && cd == is_cd &&
        (ppa_status(music) == PPA_PLAYING || ppa_status(music) == PPA_HEADERS)) {
        ppa_set_loop(music, loop);
        return TRUE;
    }
    file = UTIL_OpenFile(PAL_va(0, "ogg%s%.2d.ogg", PAL_NATIVE_PATH_SEPARATOR, track));
    current_track = track;
    is_cd = cd;
    error_reported = 0;
    /* Fade-in only. Unlike RIX, the existing Ogg backend has no crossfade. */
    if (!(fade > 0.0f)) fade = 0;
    if (fade > 10.0f) fade = 10.0f;
    fade_frames = (unsigned)(fade * (float)PPA_OUTPUT_RATE);
    fade_position = 0;
    if (ppa_start(music, file, loop)) {
        fprintf(stderr, "Pocket Ogg %d: %s\n", track, ppa_error(music));
        error_reported = 1;
        current_track = -1;
        is_cd = 0;
        return FALSE;
    }
    return TRUE;
}

VOID AUDIO_PlayMusic(INT track, BOOL loop, FLOAT fade)
{
    (void)play_track(track, loop, fade, 0);
}
BOOL AUDIO_CD_Available(VOID) { return gConfig.eCDType != CD_NONE; }
BOOL AUDIO_PlayCDTrack(INT track)
{
    if (track == -2) return is_cd && current_track > PAL_CDTRACK_BASE &&
        (ppa_status(music) == PPA_HEADERS || ppa_status(music) == PPA_PLAYING ||
         ppa_queued(music) > 0);
    if (track <= 0) return is_cd ? play_track(-1, FALSE, 0, 1) : TRUE;
    if (!AUDIO_CD_Available()) return FALSE;
    return play_track(PAL_CDTRACK_BASE + track, TRUE, 0, 1);
}
VOID AUDIO_PlaySound(INT sound)
{
    if (gAudioDevice.fOpened && gAudioDevice.fSoundEnabled &&
        gAudioDevice.pSoundPlayer && sound != INT_MIN)
        gAudioDevice.pSoundPlayer->Play(gAudioDevice.pSoundPlayer, abs(sound), FALSE, 0);
}
SDL_AudioSpec *AUDIO_GetDeviceSpec(VOID) { return &gAudioDevice.spec; }
VOID AUDIO_IncreaseVolume(VOID)
{
    gConfig.iMusicVolume += 3; gConfig.iSoundVolume += 3; set_volumes();
}
VOID AUDIO_DecreaseVolume(VOID)
{
    gConfig.iMusicVolume -= 3; gConfig.iSoundVolume -= 3; set_volumes();
}
VOID AUDIO_EnableMusic(BOOL enable) { gAudioDevice.fMusicEnabled = enable; }
BOOL AUDIO_MusicEnabled(VOID) { return gAudioDevice.fMusicEnabled; }
VOID AUDIO_EnableSound(BOOL enable) { gAudioDevice.fSoundEnabled = enable; }
BOOL AUDIO_SoundEnabled(VOID) { return gAudioDevice.fSoundEnabled; }
void AUDIO_Lock(void) { ++lock_depth; }
void AUDIO_Unlock(void) { if (lock_depth) --lock_depth; }
