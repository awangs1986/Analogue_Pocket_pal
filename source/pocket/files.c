/* SPDX-License-Identifier: GPL-3.0-only
 * One bounded, uncompressed asset archive; genuine stdio cookie streams.
 * Only fopen/access are intercepted. No SDK or libc internals are replaced.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "files.h"
#ifndef PAL_POCKET_FILES_HOST
#include "main.h"
#endif
#ifndef PAL_POCKET_FILES_HOST
#include "pocket_log.h"
#define FLOG(...) PAL_PocketLog(__VA_ARGS__)
#else
#define FLOG(...) ((void)0)
#endif
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#ifndef PAL_POCKET_FILES_HOST
#include "of_caps.h"
#include "of_file.h"
#endif

#if defined(__GLIBC__)
typedef off64_t pal_cookie_offset;
#else
typedef off_t pal_cookie_offset;
#endif

#define HEADER_SIZE 32u
#define RECORD_SIZE 128u
#define MAX_ARCHIVE_SIZE 0x7fffffffu

extern FILE *__real_fopen(const char *, const char *);

typedef struct {
    char name[PAL_POCKET_ARCHIVE_NAME_SIZE];
    uint32_t offset, size;
} asset_entry;
typedef struct {
    const asset_entry *entry;
    uint32_t position;
} asset_stream;
typedef struct {
    unsigned char *data;
    uint32_t size, position;
    const char *filename;
    unsigned index;
    int readable, append, failed;
} save_stream;

static FILE *archive;
static asset_entry *entries;
static uint32_t entry_count;
static unsigned open_assets;
static unsigned saves_open;
static char last_error[192];
static const char *const save_names[] = {
    "PAL_1.sav", "PAL_2.sav", "PAL_3.sav", "PAL_4.sav", "PAL_5.sav", "PAL.cfg"
};

static void write_error(const char *operation, const char *name)
{
    if (!last_error[0])
        snprintf(last_error, sizeof(last_error), "%s: %s (error %d)",
                 operation, name ? name : "", errno);
}
const char *PAL_PocketFileError(void) { return last_error[0] ? last_error : NULL; }
void PAL_PocketFileClearError(void) { last_error[0] = 0; }
static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* APF never needs a host absolute path. Slash-rooted paths refer to the
 * archive root. Dot segments are accepted; parent traversal and drive names
 * are rejected rather than allowing basename aliases to hide collisions. */
static int normalize(const char *src, char *out)
{
    size_t used = 0;
    if (!src) { errno = EINVAL; return -1; }
    while (*src) {
        const char *start;
        size_t len, i;
        while (*src == '/' || *src == '\\') ++src;
        start = src;
        while (*src && *src != '/' && *src != '\\') ++src;
        len = (size_t)(src - start);
        if (!len || (len == 1 && start[0] == '.')) continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            errno = EINVAL; return -1;
        }
        if (used && used + 1 < PAL_POCKET_ARCHIVE_NAME_SIZE) out[used++] = '/';
        if (len >= PAL_POCKET_ARCHIVE_NAME_SIZE - used) {
            errno = ENAMETOOLONG; return -1;
        }
        for (i = 0; i < len; ++i) {
            unsigned char c = (unsigned char)start[i];
            if (c < 32 || c > 126 || c == ':') { errno = EINVAL; return -1; }
            out[used++] = c >= 'A' && c <= 'Z' ? (char)(c + 'a' - 'A') : (char)c;
        }
    }
    out[used] = 0;
    if (!used) { errno = EINVAL; return -1; }
    return 0;
}

static const asset_entry *find_asset(const char *name)
{
    uint32_t lo = 0, hi = entry_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(name, entries[mid].name);
        if (!cmp) return &entries[mid];
        if (cmp < 0) hi = mid; else lo = mid + 1;
    }
    errno = ENOENT;
    return NULL;
}

void PAL_PocketFilesShutdown(void)
{
    /* Refuse to invalidate active cookie pointers. Callers must close them. */
    if (open_assets || saves_open) {
        errno = EBUSY;
        write_error("Files still open at shutdown", "pal.pak");
        return;
    }
    if (archive) fclose(archive);
    archive = NULL;
    free(entries);
    entries = NULL;
    entry_count = 0;
}

int PAL_PocketFilesInitPath(const char *path)
{
    unsigned char header[HEADER_SIZE], record[RECORD_SIZE];
    uint32_t count, total, previous_end, i;
    long length;
    if (archive || open_assets || saves_open) { errno = EBUSY; return -1; }
    PAL_PocketFileClearError();
    archive = __real_fopen(path, "rb");
    if (!archive) goto invalid;
    if (fseek(archive, 0, SEEK_END) || (length = ftell(archive)) < 0 ||
        length > MAX_ARCHIVE_SIZE || fseek(archive, 0, SEEK_SET)) goto invalid;
    if (fread(header, 1, sizeof(header), archive) != sizeof(header)) goto malformed;
    count = le32(header + 12);
    total = le32(header + 24);
    if (memcmp(header, "PALPAK1\0", 8) || le32(header + 8) != 1 ||
        !count || count > PAL_POCKET_ARCHIVE_MAX_FILES ||
        le32(header + 16) != HEADER_SIZE ||
        le32(header + 20) != count * RECORD_SIZE ||
        le32(header + 28) || total != (uint32_t)length ||
        total < HEADER_SIZE + count * RECORD_SIZE) goto malformed;
    entries = calloc(count, sizeof(*entries));
    if (!entries) goto invalid;
    previous_end = HEADER_SIZE + count * RECORD_SIZE;
    for (i = 0; i < count; ++i) {
        char canonical[PAL_POCKET_ARCHIVE_NAME_SIZE];
        unsigned char *nul;
        size_t n;
        if (fread(record, 1, sizeof(record), archive) != sizeof(record)) goto malformed;
        nul = memchr(record, 0, PAL_POCKET_ARCHIVE_NAME_SIZE);
        if (!nul || nul == record) goto malformed;
        n = (size_t)(nul - record);
        for (; n < PAL_POCKET_ARCHIVE_NAME_SIZE; ++n)
            if (record[n]) goto malformed;
        if (normalize((char *)record, canonical) || strcmp(canonical, (char *)record))
            goto malformed;
        memcpy(entries[i].name, record, PAL_POCKET_ARCHIVE_NAME_SIZE);
        entries[i].offset = le32(record + 120);
        entries[i].size = le32(record + 124);
        if ((i && strcmp(entries[i - 1].name, entries[i].name) >= 0) ||
            entries[i].offset < previous_end || (entries[i].offset & 15u) ||
            entries[i].offset > total || entries[i].size > total - entries[i].offset)
            goto malformed;
        previous_end = entries[i].offset + entries[i].size;
    }
    if (previous_end != total) goto malformed;
    entry_count = count;
    FLOG("pak: %s %ld bytes, %lu entries, first=%s last=%s", path, length, (unsigned long)count, entries[0].name, entries[count - 1].name);
    return 0;
malformed:
    errno = EINVAL;
invalid:
    write_error("Cannot load PAL asset archive", path);
    PAL_PocketFilesShutdown();
    return -1;
}

int PAL_PocketFilesInit(void)
{
#ifndef PAL_POCKET_FILES_HOST
    unsigned i;
    of_file_slot_register(PAL_POCKET_ASSET_SLOT, "pal.pak");
    for (i = 0; i < 5; ++i) of_file_slot_register(10 + i, save_names[i]);
    of_file_slot_register(8, save_names[5]);
#endif
    return PAL_PocketFilesInitPath("pal.pak");
}

#ifndef PAL_POCKET_FILES_HOST
/* ---- Async double-buffered read-ahead for streamed assets (AVI) ----
 * One bridge DMA read may run while the CPU decodes/draws.
 *
 * IMPORTANT (fast3 hang): CRAM0 is time-shared between CPU and bridge; while
 * an async read is in flight CRAM0 belongs to the bridge and any CPU access
 * to it (our old CRAM0 staging buffers, or the save/log slots) stalls. So
 * the destinations are now ordinary SDRAM buffers: the OS DMAs into its own
 * bounce window and copies out after completion, and the CPU never touches
 * CRAM0 while a read is outstanding. Every other file/save/log access first
 * waits for the in-flight read (PAL_PocketFilesQuiesce). Waits time out
 * (3 s; the OS ages a dead transfer out at 2 s) and then read-ahead is
 * disabled for good and the stream falls back to synchronous reads. */
void PAL_PocketAudioDrain(void);
uint32_t PAL_PocketTimeUs(void);
enum { RA_BUFS = 2, RA_TIMEOUT_US = 3000000, RA_PF = 2 };
/* Background RAM mirror of one asset (sounds.mkf): 64 KiB blocks fetched by
 * async reads whenever the bridge is idle, so the first play of a sound
 * effect no longer waits ~50 ms on SD. */
static const asset_entry *pf_entry;
static uint8_t *pf_data, *pf_have;
static uint32_t pf_blocks, pf_next, pf_pending, pf_loaded, pf_t0;
static void pf_complete(int result)
{
    if (!pf_entry) return;
    if (result >= 0 && !pf_have[pf_pending]) { pf_have[pf_pending] = 1; ++pf_loaded; }
    if (pf_loaded == pf_blocks)
        FLOG("prefetch: %s fully cached (%lu KiB) in %lu ms", pf_entry->name,
             (unsigned long)(pf_entry->size / 1024), (unsigned long)((PAL_PocketTimeUs() - pf_t0) / 1000));
}
static uint8_t *ra_buf[RA_BUFS];
static uint32_t ra_block;
static int ra_ready_init, ra_ok, ra_disabled;
static volatile int ra_inflight = -1, ra_done = 1, ra_result;
static uint32_t ra_stat_reads, ra_stat_wait_us, ra_stat_wait_max, ra_stat_hits, ra_stat_sync, ra_stat_busy;
static void ra_callback(int token, int result) { (void)token; ra_result = result; ra_done = 1; }
/* Returns 0 when idle, -1 after a timeout (read-ahead then disabled). */
static int ra_wait_idle(void);
static int ra_wait_idle(void)
{
    uint32_t t0, w;
    if (ra_inflight < 0) return 0;
    t0 = PAL_PocketTimeUs();
    while (!ra_done) {
        of_file_async_poll();
        if (ra_done) break;
        PAL_PocketAudioDrain();
        if ((uint32_t)(PAL_PocketTimeUs() - t0) > RA_TIMEOUT_US) {
            { int pf = ra_inflight == RA_PF; ra_inflight = -1; if (pf) pf_complete(-1); }
            ra_disabled = 1;
            ra_result = -1;
            ra_done = 1;
            FLOG("readahead: TIMEOUT waiting for async read; disabled, using sync reads");
            return -1;
        }
    }
    w = PAL_PocketTimeUs() - t0;
    ra_stat_wait_us += w;
    if (w > ra_stat_wait_max) ra_stat_wait_max = w;
    { int pf = ra_inflight == RA_PF; ra_inflight = -1; if (pf) pf_complete(ra_result); }
    return ra_result < 0 ? -1 : 0;
}
void PAL_PocketFilesQuiesce(void) { (void)ra_wait_idle(); }
static int ra_init(void)
{
    uint32_t maxr;
    int i;
    if (ra_disabled) return 0;
    if (ra_ready_init) return ra_ok;
    ra_ready_init = 1;
    maxr = of_file_async_max_read();
    ra_block = 64u * 1024u;
    if (maxr && ra_block > maxr) ra_block = maxr & ~511u;
    if (!maxr || ra_block < 8192u) { FLOG("readahead: unavailable (max %u)", (unsigned)maxr); return ra_ok = 0; }
    for (i = 0; i < RA_BUFS; ++i)
        if (!(ra_buf[i] = (uint8_t *)aligned_alloc(64, ra_block))) {
            FLOG("readahead: alloc failed"); return ra_ok = 0;
        }
    FLOG("readahead: %u x %u KiB SDRAM buffers (async max %u)", RA_BUFS, (unsigned)(ra_block / 1024), (unsigned)maxr);
    return ra_ok = 1;
}
typedef struct {
    const asset_entry *entry;
    uint32_t position;
    uint32_t off[RA_BUFS], len[RA_BUFS];
    int valid[RA_BUFS];
} ra_stream;
static ra_stream *ra_owner;
static int ra_issue(ra_stream *s, int b, uint32_t pos)
{
    uint32_t n = s->entry->size - pos;
    int tok;
    if (ra_disabled || pos >= s->entry->size) return -1;
    if (n > ra_block) n = ra_block;
    if (ra_wait_idle() < 0 && ra_disabled) return -1;
    s->valid[b] = 0; s->off[b] = pos; s->len[b] = n;
    ra_done = 0; ra_result = 0;
    tok = of_file_read_async((int)PAL_POCKET_ASSET_SLOT, s->entry->offset + pos, ra_buf[b], n, ra_callback);
    if (tok < 0) { ra_done = 1; ++ra_stat_busy; return -1; }
    ra_inflight = b;
    ++ra_stat_reads;
    return 0;
}
/* Synchronous fallback straight from the archive into dst. */
static ssize_t ra_sync_read(ra_stream *s, char *dst, size_t wanted)
{
    size_t got;
    uint32_t remaining = s->entry->size - s->position;
    if (wanted > remaining) wanted = remaining;
    if (!wanted) return 0;
    ra_wait_idle();
    if (fseek(archive, (long)(s->entry->offset + s->position), SEEK_SET)) return -1;
    got = fread(dst, 1, wanted, archive);
    s->position += (uint32_t)got;
    ++ra_stat_sync;
    return got ? (ssize_t)got : -1;
}
/* Buffer index holding pos, waiting for an in-flight read that covers it;
 * -1 when it must be fetched another way. */
static int ra_find(ra_stream *s, uint32_t pos)
{
    int b;
    if (ra_inflight == RA_PF && ra_done) { ra_inflight = -1; pf_complete(ra_result); }
    if (ra_inflight >= 0 && ra_done) {
        b = ra_inflight;
        ra_inflight = -1;
        if (ra_result >= 0) s->valid[b] = 1;
    }
    for (b = 0; b < RA_BUFS; ++b)
        if (s->valid[b] && pos >= s->off[b] && pos < s->off[b] + s->len[b]) { ++ra_stat_hits; return b; }
    if (ra_inflight >= 0 && ra_inflight < RA_BUFS && pos >= s->off[ra_inflight] && pos < s->off[ra_inflight] + s->len[ra_inflight]) {
        b = ra_inflight;
        if (ra_wait_idle() < 0) return -1;
        s->valid[b] = 1;
        return b;
    }
    /* miss (start or seek): fetch into buffer 0 and wait */
    if (ra_issue(s, 0, pos) < 0) return -1;
    if (ra_wait_idle() < 0) return -1;
    s->valid[0] = 1;
    return 0;
}
static ssize_t ra_read(void *cookie, char *dst, size_t wanted)
{
    ra_stream *s = cookie;
    size_t done = 0;
    while (done < wanted && s->position < s->entry->size) {
        int b = ra_disabled ? -1 : ra_find(s, s->position), other;
        uint32_t avail, n;
        if (b < 0) {
            ssize_t r = ra_sync_read(s, dst + done, wanted - done);
            if (r <= 0) return done ? (ssize_t)done : -1;
            done += (size_t)r;
            continue;
        }
        other = b ^ 1;
        if (ra_inflight < 0 && !ra_disabled) {
            uint32_t next = s->off[b] + s->len[b];
            if (next < s->entry->size && !(s->valid[other] && s->off[other] == next))
                ra_issue(s, other, next);
        }
        avail = s->off[b] + s->len[b] - s->position;
        n = (uint32_t)(wanted - done) < avail ? (uint32_t)(wanted - done) : avail;
        memcpy(dst + done, ra_buf[b] + (s->position - s->off[b]), n);
        done += n; s->position += n;
    }
    return (ssize_t)done;
}
static int ra_seek(void *cookie, pal_cookie_offset *offset, int whence);
static int ra_close(void *cookie)
{
    ra_wait_idle();
    FLOG("readahead close: %lu async reads, %lu hits, %lu sync, %lu busy, wait total %lu ms max %lu us%s",
         (unsigned long)ra_stat_reads, (unsigned long)ra_stat_hits, (unsigned long)ra_stat_sync,
         (unsigned long)ra_stat_busy, (unsigned long)(ra_stat_wait_us / 1000), (unsigned long)ra_stat_wait_max,
         ra_disabled ? " (DISABLED)" : "");
    ra_stat_reads = ra_stat_hits = ra_stat_sync = ra_stat_busy = ra_stat_wait_us = ra_stat_wait_max = 0;
    if (ra_owner == cookie) ra_owner = NULL;
    free(cookie);
    --open_assets;
    return 0;
}
#endif
int PAL_PocketPrefetchStart(const char *name)
{
    char canonical[PAL_POCKET_ARCHIVE_NAME_SIZE];
    const asset_entry *e;
    if (pf_entry || normalize(name, canonical) || !archive || !(e = find_asset(canonical))) return -1;
    if (!ra_init()) return -1;
    pf_blocks = (e->size + 65535u) / 65536u;
    pf_data = (uint8_t *)malloc(e->size);
    pf_have = (uint8_t *)calloc(pf_blocks ? pf_blocks : 1, 1);
    if (pf_data) { /* keep >= 16 MiB of heap for everything else */
        void *probe = malloc(16u << 20);
        if (!probe) { free(pf_data); pf_data = NULL; } else free(probe);
    }
    if (!pf_data || !pf_have) {
        free(pf_data); free(pf_have); pf_data = pf_have = NULL;
        FLOG("prefetch: no memory for %s (%lu KiB)", name, (unsigned long)(e->size / 1024));
        return -1;
    }
    pf_next = pf_loaded = 0;
    pf_t0 = PAL_PocketTimeUs();
    pf_entry = e;
    FLOG("prefetch: %s %lu KiB in %lu blocks", name, (unsigned long)(e->size / 1024), (unsigned long)pf_blocks);
    return 0;
}
/* Called from idle/delay loops (never from IRQ). Never blocks. */
void PAL_PocketPrefetchPump(void)
{
    uint32_t off, n;
    if (!pf_entry || ra_disabled || ra_owner || pf_loaded == pf_blocks) return;
    if (ra_inflight == RA_PF) {
        if (!ra_done) { of_file_async_poll(); if (!ra_done) return; }
        ra_inflight = -1;
        pf_complete(ra_result);
    }
    if (ra_inflight != -1) return;
    while (pf_next < pf_blocks && pf_have[pf_next]) ++pf_next;
    if (pf_next >= pf_blocks) return;
    off = pf_next * 65536u;
    n = pf_entry->size - off; if (n > 65536u) n = 65536u;
    ra_done = 0; ra_result = 0;
    if (of_file_read_async((int)PAL_POCKET_ASSET_SLOT, pf_entry->offset + off, pf_data + off, n, ra_callback) < 0) {
        ra_done = 1; return;
    }
    pf_pending = pf_next++;
    ra_inflight = RA_PF;
}
static int pf_covered(const asset_entry *e, uint32_t pos, uint32_t len)
{
    uint32_t b, last;
    if (e != pf_entry || !len) return 0;
    last = (pos + len - 1) / 65536u;
    for (b = pos / 65536u; b <= last; ++b) if (!pf_have[b]) return 0;
    return 1;
}
static ssize_t asset_read(void *cookie, char *dst, size_t wanted)
{
    asset_stream *s = cookie;
    size_t got;
    uint32_t remaining = s->entry->size - s->position;
    if (wanted > remaining) wanted = remaining;
    if (!wanted) return 0;
#ifndef PAL_POCKET_FILES_HOST
    if (pf_covered(s->entry, s->position, (uint32_t)wanted)) {
        memcpy(dst, pf_data + s->position, wanted);
        s->position += (uint32_t)wanted;
        return (ssize_t)wanted;
    }
    ra_wait_idle();
#endif
    if (fseek(archive, (long)(s->entry->offset + s->position), SEEK_SET)) return -1;
    got = fread(dst, 1, wanted, archive);
    s->position += (uint32_t)got;
    if (got < wanted && ferror(archive)) return got ? (ssize_t)got : -1;
    if (got < wanted) { errno = EIO; return got ? (ssize_t)got : -1; }
    return (ssize_t)got;
}

static int seek_position(uint32_t *position, uint32_t size, uint32_t limit,
                         pal_cookie_offset *offset, int whence)
{
    int64_t base;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = *position;
    else if (whence == SEEK_END) base = size;
    else { errno = EINVAL; return -1; }
    /* Compare before addition to avoid signed overflow from hostile offsets. */
    if (*offset < -base || *offset > (int64_t)limit - base) {
        errno = EINVAL; return -1;
    }
    *position = (uint32_t)(base + *offset);
    *offset = *position;
    return 0;
}
static int asset_seek(void *cookie, pal_cookie_offset *offset, int whence)
{
    asset_stream *s = cookie;
    return seek_position(&s->position, s->entry->size, s->entry->size, offset, whence);
}
static int asset_close(void *cookie)
{
    free(cookie);
    --open_assets;
    return 0;
}
FILE *PAL_PocketOpenAsset(const char *name)
{
    char canonical[PAL_POCKET_ARCHIVE_NAME_SIZE];
    const asset_entry *entry;
    asset_stream *stream;
    FILE *file;
    cookie_io_functions_t io = { asset_read, NULL, asset_seek, asset_close };
    if (normalize(name, canonical)) return NULL;
    if (!archive) { errno = ENOENT; return NULL; }
    entry = find_asset(canonical);
    if (!entry) return NULL;
    stream = calloc(1, sizeof(*stream));
    if (!stream) return NULL;
    stream->entry = entry;
    file = fopencookie(stream, "rb", io);
    if (!file) { free(stream); return NULL; }
    ++open_assets;
    return file;
}

static int mutable_index(const char *name)
{
    if (strlen(name) == 5 && name[0] >= '1' && name[0] <= '5' &&
        !strcmp(name + 1, ".rpg")) return name[0] - '1';
    if (!strcmp(name, "sdlpal.cfg")) return 5;
    return -1;
}
static int can_save(void)
{
#ifndef PAL_POCKET_FILES_HOST
    if (of_get_caps()->platform_id == OF_PLATFORM_POCKET &&
        !of_has_feature(OF_HW_SAVE_DT_WORD)) {
        errno = ENOTSUP;
        write_error("Save requires entry-resolved openfpgaOS core", "OF_HW_SAVE_DT_WORD");
        return 0;
    }
#endif
    return 1;
}
static ssize_t save_read(void *cookie, char *dst, size_t wanted)
{
    save_stream *s = cookie;
    uint32_t remaining = s->position < s->size ? s->size - s->position : 0;
    if (!s->readable) { errno = EBADF; return -1; }
    if (wanted > remaining) wanted = remaining;
    if (wanted) memcpy(dst, s->data + s->position, wanted);
    s->position += (uint32_t)wanted;
    return (ssize_t)wanted;
}
static ssize_t save_write(void *cookie, const char *src, size_t wanted)
{
    save_stream *s = cookie;
    uint32_t end;
    unsigned char *data;
    if (s->append) s->position = s->size;
    if (s->failed || wanted > PAL_POCKET_SAVE_MAX_BYTES - s->position) {
        errno = EFBIG;
        s->failed = 1;
        write_error("Save exceeds supported payload limit", s->filename);
        return -1;
    }
    if (!wanted) return 0;
    end = s->position + (uint32_t)wanted;
    if (end > s->size) {
        data = realloc(s->data, end);
        if (!data) {
            s->failed = 1;
            write_error("Save allocation failed", s->filename);
            return -1;
        }
        s->data = data;
        if (s->position > s->size) memset(data + s->size, 0, s->position - s->size);
        s->size = end;
    }
    memcpy(s->data + s->position, src, wanted);
    s->position = end;
    return (ssize_t)wanted;
}
static int save_seek(void *cookie, pal_cookie_offset *offset, int whence)
{
    save_stream *s = cookie;
    int rc = seek_position(&s->position, s->size, PAL_POCKET_SAVE_MAX_BYTES, offset, whence);
    if (rc) { s->failed = 1; write_error("Invalid save seek", s->filename); }
    return rc;
}
static int save_close(void *cookie)
{
    save_stream *s = cookie;
    int result = s->failed ? -1 : 0;
    if (!s->failed) {
        FILE *dest = __real_fopen(s->filename, "wb");
        if (!dest) {
            write_error("Cannot open save for commit", s->filename);
            result = -1;
        } else {
            if (fwrite(s->data, 1, s->size, dest) != s->size || ferror(dest)) {
                write_error("Cannot write save", s->filename);
                result = -1;
            }
            if (fclose(dest)) {
                write_error("Cannot close save", s->filename);
                result = -1;
            }
        }
    }
    saves_open &= ~(1u << s->index);
    free(s->data);
    free(s);
    return result;
}

static FILE *open_save(unsigned index, const char *mode)
{
    FILE *file;
    save_stream *s;
    cookie_io_functions_t io = { save_read, save_write, save_seek, save_close };
    if (!can_save()) return NULL;
    if (saves_open & (1u << index)) {
        errno = EBUSY;
        write_error("Save already open", save_names[index]);
        return NULL;
    }
    s = calloc(1, sizeof(*s));
    if (!s) { write_error("Save allocation failed", save_names[index]); return NULL; }
    s->filename = save_names[index];
    s->index = index;
    s->readable = strchr(mode, '+') != NULL;
    s->append = mode[0] == 'a';
    if (mode[0] != 'w') {
        FILE *source = __real_fopen(s->filename, "rb");
        if (source) {
            long length;
            if (fseek(source, 0, SEEK_END) || (length = ftell(source)) < 0 ||
                length > PAL_POCKET_SAVE_MAX_BYTES || fseek(source, 0, SEEK_SET)) {
                errno = EFBIG;
                fclose(source);
                goto fail;
            }
            s->size = (uint32_t)length;
            if (s->size) {
                s->data = malloc(s->size);
                if (!s->data || fread(s->data, 1, s->size, source) != s->size) {
                    fclose(source);
                    goto fail;
                }
            }
            if (fclose(source)) goto fail;
        } else if (mode[0] == 'r' || errno != ENOENT) goto fail;
    }
    s->position = s->append ? s->size : 0;
    file = fopencookie(s, mode, io);
    if (!file) goto fail;
    /* Propagate bounds errors at the write that caused them and avoid a
     * buffered retry accidentally committing a partly failed save. */
    setvbuf(file, NULL, _IONBF, 0);
    saves_open |= 1u << index;
    return file;
fail:
    write_error("Cannot prepare save", s->filename);
    free(s->data);
    free(s);
    return NULL;
}

static int valid_mode(const char *mode)
{
    unsigned b = 0, plus = 0;
    if (!mode || !strchr("rwa", mode[0]) || !mode[0]) return 0;
    for (++mode; *mode; ++mode) {
        if (*mode == 'b' && !b) b = 1;
        else if (*mode == '+' && !plus) plus = 1;
        else return 0;
    }
    return 1;
}
static FILE *wrap_fopen_impl(const char *name, const char *mode);
FILE *__wrap_fopen(const char *name, const char *mode)
{
    FILE *f = wrap_fopen_impl(name, mode);
    FLOG("fopen %s %s -> %s", name ? name : "(null)", mode ? mode : "(null)", f ? "ok" : "FAIL");
    return f;
}
static FILE *wrap_fopen_impl(const char *name, const char *mode)
{
#ifndef PAL_POCKET_FILES_HOST
    PAL_PocketFilesQuiesce();
#endif
    char canonical[PAL_POCKET_ARCHIVE_NAME_SIZE];
    int index, writing = mode && (mode[0] == 'w' || mode[0] == 'a' || strchr(mode, '+'));
    if (!valid_mode(mode) || normalize(name, canonical)) {
        errno = EINVAL;
        if (writing) write_error("Invalid writable filename or mode", name);
        return NULL;
    }
    index = mutable_index(canonical);
    if (writing) {
        if (index >= 0) return open_save((unsigned)index, mode);
        errno = EROFS;
        write_error("Only PAL saves and settings are writable", name);
        return NULL;
    }
    if (index >= 0) {
        FILE *file = __real_fopen(save_names[index], "rb");
        if (file) {
            long length;
            /* Pinned openfpgaOS exposes all 256 KiB for a zero-size save.
             * SDLPAL has no magic check and would load that zero-filled slot
             * as a game. Never accept the full-capacity sentinel as a file,
             * including settings on runtimes with the same fallback there. */
            if (!fseek(file, 0, SEEK_END) && (length = ftell(file)) > 0 &&
                length <= PAL_POCKET_SAVE_MAX_BYTES && !fseek(file, 0, SEEK_SET)) return file;
            fclose(file);
        }
        if (index != 5) { errno = ENOENT; return NULL; }
    }
    return PAL_PocketOpenAsset(canonical);
}
int __wrap_access(const char *name, int mode)
{
    char canonical[PAL_POCKET_ARCHIVE_NAME_SIZE];
    FILE *file;
    int index;
    if ((mode & ~(R_OK | W_OK | X_OK)) || normalize(name, canonical)) {
        errno = EINVAL; return -1;
    }
    if (mode & X_OK) { errno = EACCES; return -1; }
    index = mutable_index(canonical);
    if ((mode & W_OK) && index < 0) { errno = EROFS; return -1; }
    file = __wrap_fopen(canonical, "rb");
    if (!file) return -1;
    return fclose(file);
}

const char *PAL_PocketFindAssetSuffix(const char *suffix)
{
    uint32_t i;
    size_t ls = suffix ? strlen(suffix) : 0;
    for (i = 0; i < entry_count; ++i) {
        size_t ln = strlen(entries[i].name);
        if (ln >= ls && !strcmp(entries[i].name + ln - ls, suffix)) return entries[i].name;
    }
    return NULL;
}

int PAL_PocketHasAsset(const char *name)
{
    char canonical[PAL_POCKET_ARCHIVE_NAME_SIZE];
    return !normalize(name, canonical) && archive && find_asset(canonical) != NULL;
}

/* Parse "MessageFileName=" from one cfg stream; tolerant of BOM, CRLF,
 * spaces and key case. Returns malloc'd value or NULL. */
char *PAL_PocketCfgMessageFile(FILE *fp)
{
    char line[512];
    while (fp && fgets(line, sizeof(line), fp)) {
        char *p = line, *v, *e;
        if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
        while (*p == ' ' || *p == '\t') ++p;
        if (strncasecmp(p, "MessageFileName", 15)) continue;
        v = p + 15;
        while (*v == ' ' || *v == '\t') ++v;
        if (*v++ != '=') continue;
        while (*v == ' ' || *v == '\t') ++v;
        e = v + strlen(v);
        while (e > v && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        if (*v) return strdup(v);
    }
    return NULL;
}

#ifndef PAL_POCKET_FILES_HOST
void PAL_PocketResolveMessageFile(void)
{
    char *name = NULL;
    const char *found;
    FILE *fp;
    if (gConfig.pszMsgFile && gConfig.pszMsgFile[0]) return;
    if (PAL_PocketHasAsset("m.msg") && PAL_PocketHasAsset("word.dat")) return; /* DOS data */
    fp = PAL_PocketOpenAsset("sdlpal.cfg");
    if (fp) { name = PAL_PocketCfgMessageFile(fp); fclose(fp); }
    if (name && !PAL_PocketHasAsset(name)) { FLOG("msgfile: pak cfg names missing %s", name); free(name); name = NULL; }
    if (!name && (found = PAL_PocketFindAssetSuffix("_cht.msg")) != NULL) name = strdup(found);
    if (!name && (found = PAL_PocketFindAssetSuffix(".msg")) != NULL) name = strdup(found);
    FLOG("msgfile: settings slot had none; using %s", name ? name : "(none found)");
    if (name) { free(gConfig.pszMsgFile); gConfig.pszMsgFile = name; }
}
#endif

#ifndef PAL_POCKET_FILES_HOST
static int ra_seek(void *cookie, pal_cookie_offset *offset, int whence)
{
    ra_stream *s = cookie;
    return seek_position(&s->position, s->entry->size, s->entry->size, offset, whence);
}
FILE *PAL_PocketOpenAssetReadAhead(const char *name)
{
    char canonical[PAL_POCKET_ARCHIVE_NAME_SIZE];
    const asset_entry *entry;
    ra_stream *stream;
    FILE *file;
    cookie_io_functions_t io = { ra_read, NULL, ra_seek, ra_close };
    if (!ra_init() || ra_owner) return PAL_PocketOpenAsset(name);
    if (normalize(name, canonical) || !archive) return PAL_PocketOpenAsset(name);
    entry = find_asset(canonical);
    if (!entry) return NULL;
    stream = calloc(1, sizeof(*stream));
    if (!stream) return NULL;
    stream->entry = entry;
    file = fopencookie(stream, "rb", io);
    if (!file) { free(stream); return NULL; }
    ra_owner = stream;
    ++open_assets;
    return file;
}
#endif
