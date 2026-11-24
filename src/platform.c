#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L /* fileno, fsync, open, termios */
#endif

#include "platform.h"

#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* windows.h must come first */
#include <wincrypt.h>
#else
#include <fcntl.h>
#include <termios.h>
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

BbStatus platform_random_bytes(void *buf, size_t len) {
    if (!buf)
        return BB_ERR_INVALID_ARG;
#ifdef _WIN32
    /* CryptGenRandom is available on every Windows version and every MinGW flavour. */
    HCRYPTPROV provider;
    if ((DWORD)len != len)
        return BB_ERR_INVALID_ARG;
    if (!CryptAcquireContextA(&provider, NULL, NULL, PROV_RSA_FULL,
                              CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
        return BB_ERR_IO;
    BOOL ok = CryptGenRandom(provider, (DWORD)len, buf);
    CryptReleaseContext(provider, 0);
    return ok ? BB_OK : BB_ERR_IO;
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f)
        return BB_ERR_IO;
    size_t got = fread(buf, 1, len, f);
    fclose(f);
    return got == len ? BB_OK : BB_ERR_IO;
#endif
}

int platform_stdin_echo(int enable) {
#ifdef _WIN32
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    if (in == INVALID_HANDLE_VALUE || !GetConsoleMode(in, &mode))
        return 0;
    if (enable)
        mode |= ENABLE_ECHO_INPUT;
    else
        mode &= ~(DWORD)ENABLE_ECHO_INPUT;
    return SetConsoleMode(in, mode) != 0;
#else
    struct termios t;
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &t) != 0)
        return 0;
    if (enable)
        t.c_lflag |= ECHO;
    else
        t.c_lflag &= ~(tcflag_t)ECHO;
    return tcsetattr(STDIN_FILENO, TCSANOW, &t) == 0;
#endif
}

int platform_stdout_ansi(void) {
#ifdef _WIN32
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004 /* missing from older SDK headers */
#endif
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    if (out == INVALID_HANDLE_VALUE || !GetConsoleMode(out, &mode))
        return 0; /* redirected to a file or pipe */
    if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING)
        return 1;
    /* Fails on consoles older than Windows 10, which cannot show colours this way. */
    return SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return isatty(STDOUT_FILENO);
#endif
}
