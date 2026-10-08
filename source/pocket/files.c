/* SPDX-License-Identifier: GPL-3.0-only
 * One bounded, uncompressed asset archive; genuine stdio cookie streams.
 * Only fopen/access are intercepted. No SDK or libc internals are replaced.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "files.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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

static ssize_t asset_read(void *cookie, char *dst, size_t wanted)
{
    asset_stream *s = cookie;
    size_t got;
    uint32_t remaining = s->entry->size - s->position;
    if (wanted > remaining) wanted = remaining;
    if (!wanted) return 0;
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
FILE *__wrap_fopen(const char *name, const char *mode)
{
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
