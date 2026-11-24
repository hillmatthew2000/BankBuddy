#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L /* fileno, fsync, open */
#endif

#include "platform.h"

#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

FILE *platform_open_for_commit(const char *path) {
#ifdef _WIN32
    /* "c" (Microsoft CRT): fflush() also commits the file buffers to disk. */
    return fopen(path, "wbc");
#else
    return fopen(path, "wb");
#endif
}

BbStatus platform_sync(FILE *f) {
    if (!f)
        return BB_ERR_INVALID_ARG;
    if (fflush(f) != 0 || ferror(f))
        return BB_ERR_IO;
#ifndef _WIN32
    if (fsync(fileno(f)) != 0)
        return BB_ERR_IO;
#endif
    return BB_OK;
}

#ifndef _WIN32
/* Makes a completed rename durable by syncing the directory that holds `path`. */
static void sync_parent_dir(const char *path) {
    char dir[1024];
    const char *slash = strrchr(path, '/');
    if (!slash) {
        strcpy(dir, ".");
    } else {
        size_t len = (size_t)(slash - path);
        if (len == 0)
            len = 1; /* the root directory */
        if (len >= sizeof dir)
            return;
        memcpy(dir, path, len);
        dir[len] = '\0';
    }
    int fd = open(dir, O_RDONLY);
    if (fd >= 0) {
        (void)fsync(fd); /* best effort: some filesystems refuse to sync directories */
        close(fd);
    }
}
#endif

BbStatus platform_replace_file(const char *source, const char *target) {
    if (!source || !target)
        return BB_ERR_INVALID_ARG;
#ifdef _WIN32
    if (!MoveFileExA(source, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return BB_ERR_IO;
#else
    if (rename(source, target) != 0)
        return BB_ERR_IO;
    sync_parent_dir(target);
#endif
    return BB_OK;
}
