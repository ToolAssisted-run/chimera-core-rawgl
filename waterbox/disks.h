/* disks.h - the files on a floppy disk's image (disks.c). */
#ifndef RAWGL_DISKS_H
#define RAWGL_DISKS_H

#include <stddef.h>
#include <stdint.h>

/* called for each file: its path on the disk ("BANK01", "AUTO/START.PRG"),
 * its bytes (the callee copies what it keeps) */
typedef void (*disk_file_fn)(void *ctx, const char *path, const uint8_t *data, uint32_t size);

/* what kind of disk image this is ("FAT", "Amiga", "Atari ST MSA", "Atari ST
 * Pasti"), or NULL when it is none the core reads */
const char *disk_kind(const uint8_t *img, size_t size);
/* its files, through fn; 0 when it could not be read */
int disk_read(const uint8_t *img, size_t size, disk_file_fn fn, void *ctx);

#endif
