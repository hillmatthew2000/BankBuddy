#ifndef PLATFORM_H
#define PLATFORM_H

#include <stddef.h>
#include <stdio.h>

#include "status.h"

/* Thin wrappers over the few things the C standard library cannot do portably. */

/*
 * Opens `path` for writing a file that will later be committed with platform_sync(). On POSIX
 * systems the file is readable and writable by its owner only.
 */
FILE *platform_open_for_commit(const char *path);

/* Flushes `f` all the way to the storage device (fsync / commit-on-flush). */
BbStatus platform_sync(FILE *f);

/*
 * Atomically replaces `target` with `source`; both must be in the same directory. Readers see
 * either the old or the new file, never a partial one.
 */
BbStatus platform_replace_file(const char *source, const char *target);

/* Fills `buf` with cryptographically secure random bytes from the operating system. */
BbStatus platform_random_bytes(void *buf, size_t len);

/*
 * Turns echoing of typed characters on standard input off (enable = 0) or back on. Returns 1
 * if the setting was changed, 0 if standard input is not an interactive terminal. On POSIX
 * systems echo is also restored if the program is interrupted (Ctrl+C) while it is off.
 */
int platform_stdin_echo(int enable);

/*
 * Returns 1 if standard output is a terminal that understands ANSI escape sequences, turning
 * on their processing first where that is needed (Windows 10 consoles).
 */
int platform_stdout_ansi(void);

#endif /* PLATFORM_H */
