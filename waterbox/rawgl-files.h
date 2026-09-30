/* rawgl-files.h - the game's files and the project's settings (files.c). */
#ifndef RAWGL_FILES_H
#define RAWGL_FILES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct rawgl_settings
{
	int language;       /* rawgl's Language: 0 fr, 1 us, 2 de, 3 es, 4 it */
	long random_seed;   /* what the engine's clock reading at start gives */
};

void rawgl_settings_read(struct rawgl_settings *s);
/* unpacks the project's zip into sealed memory; 0 with the reason in err */
int rawgl_files_load(char *err, int errsize);
int rawgl_files_count(void);
/* patches/0001: a file's bytes by the path the engine asks for, or NULL */
const uint8_t *rawgl_memfs_find(const char *path, uint32_t *size);

#ifdef __cplusplus
}
#endif

#endif
