/* rawgl-files.h - the game's files and the project's settings (files.c). */
#ifndef RAWGL_FILES_H
#define RAWGL_FILES_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct stat;

struct rawgl_settings
{
	int language;         /* rawgl's Language: 0 fr, 1 us, 2 de, 3 es, 4 it */
	long random_seed;     /* what the engine's clock reading at start gives */
	int difficulty;       /* the 20th Anniversary Edition's: 0 easy, 1 normal, 2 hard */
	int remastered_audio; /* the anniversary editions' remastered sounds and music */
	int mt32;             /* the DOS release's sound effects on a Roland CM-32L */
};

void rawgl_settings_read(struct rawgl_settings *s);
/* reads the project's file (a zip unpacked into sealed memory, or a disc
 * image read in place); 0 with the reason in err */
int rawgl_files_load(char *err, int errsize);
int rawgl_files_count(void);
/* the engine's data path: "." for a folder, the image's name for a disc */
const char *rawgl_files_data_dir(void);
/* the project's SoundFont, open, or NULL without one */
FILE *rawgl_files_soundfont(void);

/* patches/0001: the engine's opens, by the path it asks for */
const uint8_t *rawgl_memfs_find(const char *path, uint32_t *size);
int rawgl_memfs_host(const char *path, uint32_t *size);
uint32_t rawgl_memfs_host_read(int id, uint32_t pos, void *ptr, uint32_t len);
int rawgl_memfs_stat(const char *path, struct stat *st);

#ifdef __cplusplus
}
#endif

#endif
