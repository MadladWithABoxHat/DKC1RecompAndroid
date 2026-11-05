#define _POSIX_C_SOURCE 200809L
#include "android_saves.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/log.h>
#define SLOG(...) __android_log_print(ANDROID_LOG_INFO, "DKC1Recomp", __VA_ARGS__)
#else
#define SLOG(...) do { fputs("[saves] ", stderr); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#endif

enum {
    kPathMax = 512,
    kSettleFrames = 30,      /* ~0.5 s unchanged before writing */
    kMaxDirtyFrames = 300,   /* ...but never hold a change longer than ~5 s */
    kRetryFrames = 120,
};

static char s_root[kPathMax];
static uint8_t s_persisted[kDkc1AndroidSramSize]; /* what is on disk */
static uint8_t s_seen[kDkc1AndroidSramSize];      /* previous frame's RAM */
static bool s_ready;
static bool s_have_disk;   /* s_persisted really came from / went to disk */
static bool s_seen_valid;
static unsigned s_stable, s_dirty_age, s_retry;

static bool join(char *out, size_t out_size, const char *name) {
    int n = snprintf(out, out_size, "%s/%s", s_root, name);
    return n > 0 && (size_t)n < out_size;
}

bool Dkc1AndroidSavesSetRoot(const char *root) {
    if (!root || !root[0] || strlen(root) >= sizeof s_root - 64) return false;
    snprintf(s_root, sizeof s_root, "%s", root);
    size_t n = strlen(s_root);
    while (n > 1 && s_root[n - 1] == '/') s_root[--n] = '\0';
    if (mkdir(s_root, 0700) != 0 && errno != EEXIST) return false;
    s_ready = false;
    return true;
}

/* 1 = read exactly `size` bytes, 0 = file missing, -1 = present but unusable */
static int read_exact(const char *path, uint8_t *buf, size_t size) {
    FILE *f = fopen(path, "rb");
    if (!f) return errno == ENOENT ? 0 : -1;
    bool ok = fread(buf, 1, size, f) == size && fgetc(f) == EOF && !ferror(f);
    fclose(f);
    return ok ? 1 : -1;
}

static void sync_dir(void) {
    int fd = open(s_root, O_RDONLY);
    if (fd >= 0) { fsync(fd); close(fd); }
}

/* Write to a temp file in the same directory, flush it to storage, then
 * atomically replace the target. A crash leaves either the old or the new
 * file, never a partial one. */
static bool write_atomic(const char *name, const uint8_t *data, size_t size) {
    char path[kPathMax + 16], tmp[kPathMax + 64];
    if (!join(path, sizeof path, name)) return false;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    bool ok = true;
    size_t done = 0;
    while (ok && done < size) {
        ssize_t w = write(fd, data + done, size - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) ok = false; else done += (size_t)w;
    }
    if (ok && fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;
    if (ok && rename(tmp, path) != 0) ok = false;
    if (!ok) { unlink(tmp); return false; }
    sync_dir();
    return true;
}

bool Dkc1AndroidSramLoad(uint8_t *ram, size_t size) {
    s_ready = false; s_have_disk = false; s_seen_valid = false;
    s_stable = s_dirty_age = s_retry = 0;
    if (!ram || size != kDkc1AndroidSramSize || !s_root[0]) return false;

    char path[kPathMax + 16], bak[kPathMax + 64], bad[kPathMax + 64];
    if (!join(path, sizeof path, "save.srm")) return false;
    snprintf(bak, sizeof bak, "%s.bak", path);
    snprintf(bad, sizeof bad, "%s.corrupt", path);

    uint8_t loaded[kDkc1AndroidSramSize];
    int r = read_exact(path, loaded, size);
    if (r == 1) {
        SLOG("loaded save.srm");
    } else if (r == -1) {
        /* Main file damaged (e.g. torn write by an older build). Keep it for
         * recovery, restore from the backup if there is one. */
        rename(path, bad);
        r = read_exact(bak, loaded, size);
        if (r == 1) SLOG("save.srm damaged; restored from save.srm.bak");
        else SLOG("save.srm damaged and no usable backup; starting fresh");
    } else {
        /* Missing main file: an interrupted older write may have left a .bak. */
        r = read_exact(bak, loaded, size);
        if (r == 1) SLOG("save.srm missing; restored from save.srm.bak");
    }
    if (r == 1) {
        memcpy(ram, loaded, size);
        memcpy(s_persisted, loaded, size);
        s_have_disk = true;
    } else {
        memcpy(s_persisted, ram, size); /* fresh cartridge contents */
    }
    s_ready = true;
    return true;
}

bool Dkc1AndroidSramFlush(const uint8_t *ram, size_t size) {
    if (!s_ready || !ram || size != kDkc1AndroidSramSize) return true;
    if (memcmp(ram, s_persisted, size) == 0) return true;
    /* Keep the previous good image as save.srm.bak so an earlier save can be
     * recovered by hand if a newer one turns out to be bad. */
    if (s_have_disk) write_atomic("save.srm.bak", s_persisted, size);
    if (!write_atomic("save.srm", ram, size)) {
        SLOG("could not write save.srm; previous file kept");
        return false;
    }
    memcpy(s_persisted, ram, size);
    s_have_disk = true;
    s_seen_valid = false;
    s_stable = s_dirty_age = s_retry = 0;
    SLOG("saved save.srm");
    return true;
}

void Dkc1AndroidSramTick(const uint8_t *ram, size_t size) {
    if (!s_ready || !ram || size != kDkc1AndroidSramSize) return;
    if (memcmp(ram, s_persisted, size) == 0) {
        s_seen_valid = false; s_stable = s_dirty_age = 0;
        return;
    }
    if (!s_seen_valid || memcmp(ram, s_seen, size) != 0) {
        memcpy(s_seen, ram, size);
        s_seen_valid = true;
        s_stable = 0;
    } else {
        s_stable++;
    }
    s_dirty_age++;
    if (s_retry) { s_retry--; return; }
    if (s_stable >= kSettleFrames || s_dirty_age >= kMaxDirtyFrames) {
        if (!Dkc1AndroidSramFlush(ram, size)) s_retry = kRetryFrames;
    }
}
