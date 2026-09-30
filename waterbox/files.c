/* files.c - the game's data files, out of the project's zip, and the settings.
 *
 * A project brings Another World as one file: a .zip of the game's folder
 * (the "game" slot, file_slots.json). Init unpacks it once into sealed memory
 * - read-only after Init, and so never part of a savestate - and the engine's
 * every open (rawgl's File, patches/0001) is answered from there by
 * rawgl_memfs_find(). The zip may hold the files at its top or in a folder:
 * the game's folder is the one holding MEMLIST.BIN or BANK01 (the Atari
 * demo's AW.TOS), and names are matched without regard to case, as rawgl does
 * on disk.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "miniz.h"
#include "rawgl-files.h"

struct gfile
{
	char path[256];   /* relative to the game's folder, as stored */
	const uint8_t *data;
	uint32_t size;
};

static struct gfile *g_files;
static int g_nfiles;
static char g_zip_name[256];

/* ------------------------------------------------------------ settings */

void rawgl_settings_read(struct rawgl_settings *s)
{
	char lang[16] = "us";
	wbx_setting_str("language", lang, (int)sizeof lang);
	s->language = !strcmp(lang, "fr") ? 0 : !strcmp(lang, "de") ? 2 : !strcmp(lang, "es") ? 3 : !strcmp(lang, "it") ? 4 : 1;
	s->random_seed = wbx_setting_long("randomSeed", 0);
}

/* ------------------------------------------------------------ the zip */

static const char *base_name(const char *path)
{
	const char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static int depth(const char *path)
{
	int d = 0;
	for (; *path; path++) d += *path == '/';
	return d;
}

static long read_whole(const char *name, uint8_t **out)
{
	FILE *f = fopen(name, "rb");
	if (!f) return -1;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *b = n > 0 ? malloc((size_t)n) : NULL;
	if (!b || fread(b, 1, (size_t)n, f) != (size_t)n)
	{
		fclose(f);
		free(b);
		return -1;
	}
	fclose(f);
	*out = b;
	return n;
}

int rawgl_files_load(char *err, int errsize)
{
	/* the project's slot map names the file; without one, a host that is not a
	 * project's mounts it as "rom" (chimera-run <package> <rom>), and the
	 * core's harnesses as game.zip */
	uint8_t *zip = NULL;
	long zip_size = -1;
	if (wbx_slot_name("game", 0, g_zip_name, (int)sizeof g_zip_name))
		zip_size = read_whole(g_zip_name, &zip);
	else
	{
		snprintf(g_zip_name, sizeof g_zip_name, "rom");
		zip_size = read_whole(g_zip_name, &zip);
		if (zip_size < 0)
		{
			snprintf(g_zip_name, sizeof g_zip_name, "game.zip");
			zip_size = read_whole(g_zip_name, &zip);
		}
	}
	if (zip_size < 0)
	{
		snprintf(err, (size_t)errsize, "Another World needs the game's files: add a .zip of the game's folder to the project (%s is not there)", g_zip_name);
		return 0;
	}

	mz_zip_archive za;
	memset(&za, 0, sizeof za);
	if (!mz_zip_reader_init_mem(&za, zip, (size_t)zip_size, 0))
	{
		snprintf(err, (size_t)errsize, "%s is not a zip file: the game's files come as a .zip of its folder", g_zip_name);
		free(zip);
		return 0;
	}
	const int n = (int)mz_zip_reader_get_num_files(&za);

	/* the game's folder: where the shallowest MEMLIST.BIN / BANK01 / AW.TOS is */
	char root[256] = "";
	int best = 1 << 30, found = 0;
	for (int i = 0; i < n; i++)
	{
		mz_zip_archive_file_stat st;
		if (!mz_zip_reader_file_stat(&za, (mz_uint)i, &st) || st.m_is_directory) continue;
		const char *b = base_name(st.m_filename);
		if (strcasecmp(b, "memlist.bin") && strcasecmp(b, "bank01") && strcasecmp(b, "aw.tos")) continue;
		const int d = depth(st.m_filename);
		if (d < best)
		{
			best = d;
			found = 1;
			snprintf(root, sizeof root, "%.*s", (int)(b - st.m_filename), st.m_filename);
		}
	}
	if (!found)
	{
		snprintf(err, (size_t)errsize, "%s holds no Another World data: the game's folder has MEMLIST.BIN and the BANK files (DOS), or the BANK files alone (Amiga, Atari ST)", g_zip_name);
		mz_zip_reader_end(&za);
		free(zip);
		return 0;
	}

	const size_t rootlen = strlen(root);
	g_files = calloc((size_t)n, sizeof *g_files);
	for (int i = 0; i < n; i++)
	{
		mz_zip_archive_file_stat st;
		if (!mz_zip_reader_file_stat(&za, (mz_uint)i, &st) || st.m_is_directory) continue;
		if (strncmp(st.m_filename, root, rootlen) != 0 || strlen(st.m_filename + rootlen) >= sizeof g_files[0].path) continue;
		if (st.m_uncomp_size > 64u << 20)
		{
			snprintf(err, (size_t)errsize, "%s in %s is too large for the game's data (%llu bytes)", st.m_filename, g_zip_name, (unsigned long long)st.m_uncomp_size);
			mz_zip_reader_end(&za);
			free(zip);
			return 0;
		}
		uint8_t *data = alloc_sealed(st.m_uncomp_size ? (size_t)st.m_uncomp_size : 1);
		if (!data || !mz_zip_reader_extract_to_mem(&za, (mz_uint)i, data, (size_t)st.m_uncomp_size, 0))
		{
			snprintf(err, (size_t)errsize, "%s in %s could not be unpacked", st.m_filename, g_zip_name);
			mz_zip_reader_end(&za);
			free(zip);
			return 0;
		}
		struct gfile *g = &g_files[g_nfiles++];
		snprintf(g->path, sizeof g->path, "%s", st.m_filename + rootlen);
		g->data = data;
		g->size = (uint32_t)st.m_uncomp_size;
	}
	mz_zip_reader_end(&za);
	free(zip);
	return 1;
}

int rawgl_files_count(void) { return g_nfiles; }

/* the engine asks for "<datapath>/<name>", its data path being "." - so
 * "./bank01", "./Data/Pak01.pak": the leading "./" and any "." folders go, and
 * the rest is matched without regard to case */
const uint8_t *rawgl_memfs_find(const char *path, uint32_t *size)
{
	char want[256];
	size_t w = 0;
	while (*path && w < sizeof want - 1)
	{
		if (path[0] == '.' && path[1] == '/') { path += 2; continue; }
		if (path[0] == '/' && (w == 0 || want[w - 1] == '/')) { path++; continue; }
		want[w++] = *path++;
	}
	want[w] = 0;
	for (int i = 0; i < g_nfiles; i++)
		if (!strcasecmp(g_files[i].path, want))
		{
			*size = g_files[i].size;
			return g_files[i].data;
		}
	return NULL;
}
