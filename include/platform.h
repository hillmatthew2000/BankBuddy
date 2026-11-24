#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdio.h>

#include "status.h"

/* Thin wrappers over the few things the C standard library cannot do portably. */

/* Opens `path` for writing a file that will later be committed with platform_sync(). */
FILE *platform_open_for_commit(const char *path);

/* Flushes `f` all the way to the storage device (fsync / commit-on-flush). */
BbStatus platform_sync(FILE *f);

/*
 * Atomically replaces `target` with `source`; both must be in the same directory. Readers see
 * either the old or the new file, never a partial one.
 */
BbStatus platform_replace_file(const char *source, const char *target);

#endif /* PLATFORM_H */
