/* files.c - the game's data files, out of the project's files, and the settings.
 *
 * A project picks a release - the "release" setting, which Chimera shows as
 * the machine (waterbox.config "machines") - and brings that release's own
 * files as firmware, each mounted under its id (k_release_files below, the
 * same list as waterbox.config's firmware; the gate checks the two agree):
 * the floppy releases' disk images (or a zip of the files on them), the 3DO's
 * disc (its image, .iso, or MAME's compressed image of it, .chd), the 15th
 * Anniversary Edition's Pak01.pak, music and texts, a zip of the 20th's game
 * folder, Windows 3.1's four files. Never an installer: where a release ships
 * its files inside one (the 15th's CD, GOG's 20th), the core asks for the
 * files, saying which. Without any of the release's files, a host that is no
 * project's - chimera-run given a rom, the core's harnesses - may still bring
 * one file (or a "game" slot of several) as before.
 *
 *   - A zip (or a loose file) is unpacked once, at Init, into sealed memory - read-only after
 *     Init, and so never part of a savestate - and the engine's every open
 *     (rawgl's File, patches/0001) is answered from there by
 *     rawgl_memfs_find(). The zip may hold the game's folder at its top or
 *     inside a folder: the game's folder is the shallowest one holding what a
 *     release rawgl knows is recognised by (rawgl's Resource::detectVersion):
 *     MEMLIST.BIN (DOS), BANK01 (Amiga, Atari ST), AW.TOS (the Atari ST demo),
 *     Data/Pak01.pak (the 15th Anniversary Edition), game/DAT/FILE017.DAT (the
 *     20th), BANK (Windows 3.1) or GameData/File340 (3DO), or a disc image.
 *     Names are matched without regard to case, as rawgl does on disk.
 *   - A disc is too large to copy for nothing: it is read where it is mounted
 *     (rawgl_memfs_host_read), each read opening, reading and closing it, so
 *     no host handle or file position outlives a call - miniBox's savestates
 *     carry the machine's memory, not the host's open files. What it keeps
 *     between reads is guest memory: a cache of the last 64 KiB of an image,
 *     or of a .chd the last hunk libchdr decompressed. Of a .chd, rawgl sees
 *     the first data track's 2048-byte sectors, as an image holds them.
 *
 * The engine's data path is "." for a folder, or the image's name (rawgl
 * opens an .iso it is given as its data path, resource_3do.cpp).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include <libchdr/cdrom.h>
#include <libchdr/chd.h>

#include "disks.h"
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
static char g_data_dir[256] = ".";

/* the disc, read in place: an image, or a .chd */
static char g_host_name[256];     /* its mount */
static char g_host_path[256];     /* what the engine calls it */
static uint32_t g_host_size;
static int g_has_host;
#define HOST_CACHE 65536
static uint8_t g_host_cache[HOST_CACHE];
static uint32_t g_host_cache_pos, g_host_cache_len;

/* a .chd: libchdr over the mounted file (chd_io_*), the data track's frames */
static chd_file *g_chd;
static uint32_t g_chd_first;      /* the track's first sector, in frames from the start */
static uint32_t g_chd_data;       /* where a frame's 2048 bytes start in it: 16 raw, 0 cooked */
static uint32_t g_chd_fph;        /* frames a hunk */
static uint8_t *g_chd_hunk;
static int64_t g_chd_hunknum = -1;
static struct { int64_t pos; } g_chd_io;

/* ------------------------------------------------------------ settings */

void rawgl_settings_read(struct rawgl_settings *s)
{
	char str[16] = "us";
	wbx_setting_str("language", str, (int)sizeof str);
	s->language = !strcmp(str, "fr") ? 0 : !strcmp(str, "de") ? 2 : !strcmp(str, "es") ? 3 : !strcmp(str, "it") ? 4 : 1;
	s->random_seed = wbx_setting_long("randomSeed", 0);
	snprintf(str, sizeof str, "normal");
	wbx_setting_str("difficulty", str, (int)sizeof str);
	s->difficulty = !strcmp(str, "easy") ? 0 : !strcmp(str, "hard") ? 2 : 1;
	s->remastered_audio = wbx_setting_bool("remasteredAudio", 1);
	s->mt32 = wbx_setting_bool("mt32", 0);
	s->soundfont = wbx_setting_bool("soundFont", 0);
	/* a setting counts for the releases it is shown for (waterbox.config
	 * "when"): one left on from another release asks for nothing */
	char release[16] = "dos";
	wbx_setting_str("release", release, (int)sizeof release);
	if (strcmp(release, "dos") && strcmp(release, "dosdemo")) s->mt32 = 0;
	if (strcmp(release, "win31")) s->soundfont = 0;
}

/* ------------------------------------------------------------ the releases */

static const struct { const char *id, *label; } k_releases[] = {
	{ "dos", "Another World (DOS)" },
	{ "amiga", "Another World (Amiga, English)" },
	{ "amigafr", "Another World (Amiga, French)" },
	{ "atari", "Another World (Atari ST)" },
	{ "win31", "Out of This World (Windows 3.1)" },
	{ "3do", "Out of This World (3DO)" },
	{ "15th", "Another World 15th Anniversary Edition" },
	{ "20th", "Another World 20th Anniversary Edition" },
	{ "dosdemo", "Out of This World (DOS demo)" },
	{ "stdemo", "Another World (Atari ST rolling demo)" },
};

/* each release's files: the firmware ids they are mounted under; a language
 * of its own for a file only that language needs */
static const struct { const char *release, *id, *language; } k_release_files[] = {
	{ "dos", "dos-disk", NULL },
	{ "amiga", "amiga-en-disk1", NULL },
	{ "amiga", "amiga-en-disk2", NULL },
	{ "amigafr", "amiga-fr-disk1", NULL },
	{ "amigafr", "amiga-fr-disk2", NULL },
	{ "atari", "atari-disk1", NULL },
	{ "atari", "atari-disk2", NULL },
	{ "win31", "BANK", NULL },
	{ "win31", "WORLD.EXE", NULL },
	{ "win31", "X.MID", NULL },
	{ "win31", "Y.MID", NULL },
	{ "3do", "3do-disc", NULL },
	{ "15th", "Pak01.pak", NULL },
	{ "15th", "Intro2004.ogg", NULL },
	{ "15th", "End2004.ogg", NULL },
	{ "15th", "lang_English.Txt", "us" },
	{ "15th", "lang_Francais.Txt", "fr" },
	{ "15th", "lang_Espanol.txt", "es" },
	{ "20th", "20th-game", NULL },
	{ "dosdemo", "dos-demo", NULL },
	{ "stdemo", "atari-demo-disk", NULL },
};


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

static int ends_with_ci(const char *s, const char *suffix)
{
	const size_t n = strlen(s), m = strlen(suffix);
	return n >= m && !strcasecmp(s + n - m, suffix);
}

/* what a release's folder is recognised by, as a path inside it; the game's
 * folder is where the path starts. A disc image's is where it lies. */
static const char *const k_markers[] = {
	"memlist.bin", "bank01", "aw.tos", "data/pak01.pak", "game/dat/file017.dat", "bank", "gamedata/file340",
	"pak01.pak",
};

/* the length of the folder part if `path` is one of the markers there, else -1 */
static int marker_root(const char *path)
{
	for (size_t i = 0; i < sizeof k_markers / sizeof k_markers[0]; i++)
	{
		const char *m = k_markers[i];
		const size_t n = strlen(path), k = strlen(m);
		if (n < k || strcasecmp(path + n - k, m)) continue;
		if (n > k && path[n - k - 1] != '/') continue;
		return (int)(n - k);
	}
	if (ends_with_ci(path, ".iso")) return (int)(base_name(path) - path);
	return -1;
}

/* 20th Anniversary Edition: its backgrounds come in up to eight sizes
 * (game/BGZ/data<W>x<H>/, gzip'd; GOG's installation: game/BMP/data<W>x<H>/),
 * of which the original renderer draws only the 320x200 ones (rawgl
 * video.cpp copyBitmapPtr): the others are not unpacked */
static int skipped(const char *rel)
{
	if (!strncasecmp(rel, "game/bgz/data", 13)) return strncasecmp(rel, "game/bgz/data320x200/", 21) != 0;
	if (!strncasecmp(rel, "game/bmp/data", 13)) return strncasecmp(rel, "game/bmp/data320x200/", 21) != 0;
	return 0;
}

/* the start of a file: its size, or -1 when it is not there */
static long peek(const char *name, uint8_t *buf, size_t len)
{
	FILE *f = fopen(name, "rb");
	if (!f) return -1;
	memset(buf, 0, len);
	size_t got = fread(buf, 1, len, f);
	(void)got;
	fseek(f, 0, SEEK_END);
	const long n = ftell(f);
	fclose(f);
	return n;
}

/* libchdr's file: the mounted .chd, opened, read and closed at each read */
static uint64_t chd_io_size(void *p)
{
	(void)p;
	FILE *f = fopen(g_host_name, "rb");
	if (!f) return (uint64_t)-1;
	fseek(f, 0, SEEK_END);
	const long n = ftell(f);
	fclose(f);
	return (uint64_t)n;
}
static size_t chd_io_read(void *buf, size_t size, size_t count, void *p)
{
	(void)p;
	FILE *f = fopen(g_host_name, "rb");
	if (!f) return 0;
	size_t got = 0;
	if (fseek(f, (long)g_chd_io.pos, SEEK_SET) == 0) got = fread(buf, size, count, f);
	fclose(f);
	g_chd_io.pos += (int64_t)(got * size);
	return got;
}
static int chd_io_close(void *p) { (void)p; return 0; }
static int chd_io_seek(void *p, int64_t off, int whence)
{
	(void)p;
	if (whence == SEEK_SET) g_chd_io.pos = off;
	else if (whence == SEEK_CUR) g_chd_io.pos += off;
	else g_chd_io.pos = (int64_t)chd_io_size(p) + off;
	return 0;
}
static const core_file_callbacks k_chd_io = { chd_io_size, chd_io_read, chd_io_close, chd_io_seek };

/* the first data track of a CD image: its frames and where its sectors are
 * (chdman's layout: each track padded to 4 frames; a pregap the image holds -
 * its type starting with V - comes before the track's first sector) */
static int open_chd(char *err, int errsize)
{
	if (chd_open_core_file_callbacks(&k_chd_io, NULL, CHD_OPEN_READ, NULL, &g_chd) != CHDERR_NONE)
	{
		snprintf(err, (size_t)errsize, "%s is not a CHD libchdr can read", g_zip_name);
		return 0;
	}
	const chd_header *h = chd_get_header(g_chd);
	if (h->unitbytes != CD_FRAME_SIZE || h->hunkbytes % CD_FRAME_SIZE)
	{
		snprintf(err, (size_t)errsize, "%s is a CHD of a hard disk or a laserdisc, not of a CD", g_zip_name);
		return 0;
	}
	g_chd_fph = h->hunkbytes / CD_FRAME_SIZE;
	uint32_t start = 0;
	for (uint32_t i = 0;; i++)
	{
		char meta[256];
		uint32_t len = 0, tag = 0;
		uint8_t flags;
		if (chd_get_metadata(g_chd, CDROM_TRACK_METADATA2_TAG, i, meta, sizeof meta - 1, &len, &tag, &flags) != CHDERR_NONE)
			break;
		meta[len < sizeof meta ? len : sizeof meta - 1] = 0;
		int track = 0, frames = 0, pregap = 0, postgap = 0;
		char type[32] = "", subtype[32] = "", pgtype[32] = "", pgsub[32] = "";
		if (sscanf(meta, CDROM_TRACK_METADATA2_FORMAT, &track, type, subtype, &frames, &pregap, pgtype, pgsub, &postgap) < 4)
			break;
		const int stored_pregap = pgtype[0] == 'V' ? pregap : 0;
		if (!strcmp(type, "MODE1_RAW") || !strcmp(type, "MODE1"))
		{
			g_chd_first = start + (uint32_t)stored_pregap;
			g_chd_data = strcmp(type, "MODE1_RAW") ? 0 : 16;
			g_host_size = (uint32_t)(frames - stored_pregap) * 2048u;
			g_chd_hunk = malloc(h->hunkbytes);
			return 1;
		}
		start += ((uint32_t)frames + CD_TRACK_PADDING - 1) / CD_TRACK_PADDING * CD_TRACK_PADDING;
	}
	snprintf(err, (size_t)errsize, "%s holds no data track (MODE1) for rawgl to read", g_zip_name);
	return 0;
}

static uint32_t chd_read_sectors(uint32_t pos, uint8_t *out, uint32_t len)
{
	uint32_t done = 0;
	while (done < len)
	{
		const uint32_t at = pos + done;
		const uint32_t frame = g_chd_first + at / 2048, in = at % 2048;
		const int64_t hunk = frame / g_chd_fph;
		if (hunk != g_chd_hunknum)
		{
			if (chd_read(g_chd, (uint32_t)hunk, g_chd_hunk) != CHDERR_NONE) break;
			g_chd_hunknum = hunk;
		}
		uint32_t n = 2048 - in;
		if (n > len - done) n = len - done;
		memcpy(out + done, g_chd_hunk + (frame % g_chd_fph) * CD_FRAME_SIZE + g_chd_data + in, n);
		done += n;
	}
	return done;
}

/* rawgl's own test for a 3DO disc (resource_3do.cpp OperaIso::readToc) */
static int is_opera_image(const uint8_t *buf)
{
	return buf[0] == 1 && !memcmp(buf + 40, "CD-ROM", 6);
}

/* sector 16 of a PC CD: ISO 9660's primary volume descriptor */
static int is_iso9660_pvd(const uint8_t *sector)
{
	return sector[0] == 1 && !memcmp(sector + 1, "CD001", 5);
}

/* what the core asks for where a release comes inside an installer */
#define ASK_15TH "the 15th Anniversary Edition's Pak01.pak, with Intro2004.ogg, End2004.ogg and lang_<language>.Txt for its music and texts"
#define ASK_RAW "the game's own files from it instead: " ASK_15TH "; the 20th's game folder, zipped"
#define ASK_PC_CD "the 15th Anniversary Edition's CD holds the game inside an installer, which the core does not open: add " ASK_15TH

static int contains(const uint8_t *buf, size_t n, const char *what)
{
	const size_t k = strlen(what);
	for (size_t i = 0; i + k <= n; i++)
		if (buf[i] == (uint8_t)what[0] && !memcmp(buf + i, what, k)) return 1;
	return 0;
}

/* an installer's start (its first 256 KiB): which, or NULL. NSIS's first
 * header and Inno Setup's loader data both lie in there. */
#define INSTALLER_PEEK (256u << 10)
static const char *installer_kind(const uint8_t *buf, size_t n)
{
	if (n < 64 || buf[0] != 'M' || buf[1] != 'Z') return NULL;
	if (contains(buf, n, "NullsoftInst")) return "an NSIS installer";
	if (contains(buf, n, "Inno Setup Setup Data")) return "an Inno Setup installer";
	return NULL;
}

/* ------------------------------------------------------------ the project's files
 *
 * The files the project brings - zips, disk images (disks.c), disk images in
 * zips - make one list of candidates, the first of a name kept (a file two
 * disks both carry is the same file); the game's folder is found in it, and
 * only that folder's files are read into sealed memory. A disc (.chd, .iso)
 * is the 3DO's and comes alone. */

struct cand
{
	char path[256];
	int zip;           /* the zip it is in; -1: a disk's file, in data; -2 - n: loose file n */
	mz_uint entry;
	uint8_t *data;
	uint32_t size;
};

#define MAX_ZIPS 16
static struct
{
	FILE *f;
	mz_zip_archive za;
	char name[256];
} g_zips[MAX_ZIPS];
static int g_nzips;
#define MAX_LOOSE 8
static char g_loose[MAX_LOOSE][256];
static int g_nloose;
static struct cand *g_cands;
static int g_ncands, g_capcands;

static void add_cand(const char *path, int zip, mz_uint entry, const uint8_t *data, uint32_t size)
{
	if (strlen(path) >= sizeof g_cands[0].path) return;
	for (int i = 0; i < g_ncands; i++)
		if (!strcasecmp(g_cands[i].path, path)) return;
	if (g_ncands == g_capcands)
	{
		g_capcands = g_capcands ? g_capcands * 2 : 64;
		g_cands = realloc(g_cands, (size_t)g_capcands * sizeof *g_cands);
	}
	struct cand *c = &g_cands[g_ncands++];
	snprintf(c->path, sizeof c->path, "%s", path);
	c->zip = zip;
	c->entry = entry;
	c->size = size;
	c->data = NULL;
	if (data)
	{
		c->data = malloc(size ? size : 1);
		memcpy(c->data, data, size);
	}
}

static void disk_file(void *ctx, const char *path, const uint8_t *data, uint32_t size)
{
	(void)ctx;
	add_cand(path, -1, 0, data, size);
}

static int is_disk_name(const char *n)
{
	static const char *const exts[] = { ".adf", ".st", ".msa", ".stx", ".img", ".ima", ".dsk" };
	for (size_t i = 0; i < sizeof exts / sizeof exts[0]; i++)
		if (ends_with_ci(n, exts[i])) return 1;
	return 0;
}

#define MAX_DISK (4u << 20)

/* the zip is read from its file as miniz asks, never whole: only what it
 * unpacks takes memory */
static size_t zip_read(void *opaque, mz_uint64 ofs, void *buf, size_t n)
{
	FILE *f = opaque;
	if (fseek(f, (long)ofs, SEEK_SET) != 0) return 0;
	return fread(buf, 1, n, f);
}

static int add_zip(const char *name, long size, char *err, int errsize)
{
	if (g_nzips == MAX_ZIPS)
	{
		snprintf(err, (size_t)errsize, "too many zips in the project (%d at most)", MAX_ZIPS);
		return 0;
	}
	const int z = g_nzips;
	g_zips[z].f = fopen(name, "rb");
	snprintf(g_zips[z].name, sizeof g_zips[z].name, "%.255s", name);
	memset(&g_zips[z].za, 0, sizeof g_zips[z].za);
	g_zips[z].za.m_pRead = zip_read;
	g_zips[z].za.m_pIO_opaque = g_zips[z].f;
	if (!g_zips[z].f || !mz_zip_reader_init(&g_zips[z].za, (mz_uint64)size, 0))
	{
		if (g_zips[z].f) fclose(g_zips[z].f);
		snprintf(err, (size_t)errsize, "%s is not a zip file", name);
		return 0;
	}
	g_nzips++;
	const int n = (int)mz_zip_reader_get_num_files(&g_zips[z].za);
	for (int i = 0; i < n; i++)
	{
		mz_zip_archive_file_stat st;
		if (!mz_zip_reader_file_stat(&g_zips[z].za, (mz_uint)i, &st) || st.m_is_directory) continue;
		if (st.m_uncomp_size > 0xFFFFFFF0u)
		{
			snprintf(err, (size_t)errsize, "%s in %s is too large (%llu bytes)", st.m_filename, name, (unsigned long long)st.m_uncomp_size);
			return 0;
		}
		/* a disk image in the zip (TOSEC's come so): its files */
		if (is_disk_name(st.m_filename) && st.m_uncomp_size <= MAX_DISK)
		{
			uint8_t *img = malloc(st.m_uncomp_size ? (size_t)st.m_uncomp_size : 1);
			if (img && mz_zip_reader_extract_to_mem(&g_zips[z].za, (mz_uint)i, img, (size_t)st.m_uncomp_size, 0)
				&& disk_kind(img, (size_t)st.m_uncomp_size))
			{
				if (!disk_read(img, (size_t)st.m_uncomp_size, disk_file, NULL))
				{
					snprintf(err, (size_t)errsize, "%s in %s is a disk image the core could not read", st.m_filename, name);
					free(img);
					return 0;
				}
				free(img);
				continue;
			}
			free(img);
		}
		add_cand(st.m_filename, z, (mz_uint)i, NULL, (uint32_t)st.m_uncomp_size);
	}
	return 1;
}

static int add_disk(const char *name, long size, char *err, int errsize)
{
	if (size <= 0 || (unsigned long)size > MAX_DISK) return 0;
	FILE *f = fopen(name, "rb");
	if (!f) return 0;
	uint8_t *img = malloc((size_t)size);
	const size_t got = fread(img, 1, (size_t)size, f);
	fclose(f);
	int ok = 0;
	if (got == (size_t)size && disk_kind(img, (size_t)size))
	{
		ok = disk_read(img, (size_t)size, disk_file, NULL);
		if (!ok) snprintf(err, (size_t)errsize, "%s is a %s disk image the core could not read", name, disk_kind(img, (size_t)size));
		else ok = 1;
	}
	free(img);
	return ok;
}

/* the 15th Anniversary Edition's files as they come, out of their folders,
 * where its folder has them: Data/Pak01.pak, its music (Music/Intro2004.ogg,
 * End2004.ogg) and its texts (Menu/lang_English.Txt, ...); NULL when `name`
 * is none of those */
static const char *flat_place(const char *name, char *out, size_t cap)
{
	const char *base = base_name(name);
	if (!strcasecmp(base, "pak01.pak")) snprintf(out, cap, "Data/Pak01.pak");
	else if (ends_with_ci(base, ".ogg")) snprintf(out, cap, "Music/%.240s", base);
	else if (!strncasecmp(base, "lang_", 5) && ends_with_ci(base, ".txt")) snprintf(out, cap, "Menu/%.240s", base);
	else return NULL;
	return out;
}

/* a loose file of those: Pak01.pak is also known by its "PACK", whatever its name */
static const char *raw_place(const char *name, const uint8_t *head, long size, char *out, size_t cap)
{
	if (size >= 12 && !memcmp(head, "PACK", 4))
	{
		snprintf(out, cap, "Data/Pak01.pak");
		return out;
	}
	return flat_place(name, out, cap);
}

static int has_cand(const char *root, const char *rel)
{
	char path[512];
	snprintf(path, sizeof path, "%s%s", root, rel);
	for (int i = 0; i < g_ncands; i++)
		if (!strcasecmp(g_cands[i].path, path)) return 1;
	return 0;
}

/* a release's file mounted under its id, where the game's folder has it: the
 * 15th's (flat_place; an Intro2004.ogg that is a WAV is the other editions'
 * Music/AW/Intro2004.wav), and the rest - Windows 3.1's BANK, WORLD.EXE,
 * X.MID, Y.MID - under its id, at the folder's top */
static const char *fw_place(const char *id, const uint8_t *head, long size, char *out, size_t cap)
{
	if (size >= 12 && !memcmp(head, "PACK", 4)) snprintf(out, cap, "Data/Pak01.pak");
	else if (ends_with_ci(id, ".ogg") && size >= 12 && !memcmp(head, "RIFF", 4))
		snprintf(out, cap, "Music/AW/%.*s.wav", (int)(strlen(id) - 4), id);
	else if (!flat_place(id, out, cap)) snprintf(out, cap, "%.250s", id);
	return out;
}

static int add_loose(const char *name, const char *place, long size, char *err, int errsize)
{
	if (g_nloose == MAX_LOOSE)
	{
		snprintf(err, (size_t)errsize, "too many loose files in the project (%d at most; zip them)", MAX_LOOSE);
		return 0;
	}
	if ((unsigned long)size > 0xFFFFFFF0u)
	{
		snprintf(err, (size_t)errsize, "%s is too large (%ld bytes)", name, size);
		return 0;
	}
	snprintf(g_loose[g_nloose], sizeof g_loose[0], "%.255s", name);
	add_cand(place, -2 - g_nloose, 0, NULL, (uint32_t)size);
	g_nloose++;
	return 1;
}

/* the project holds no game: an installer where the files should be is said
 * so, and the files it holds asked for */
static int said_installer(char *err, int errsize)
{
	uint8_t *buf = malloc(INSTALLER_PEEK);
	int said = 0;
	for (int i = 0; buf && i < g_ncands && !said; i++)
	{
		const struct cand *c = &g_cands[i];
		if (c->zip < 0 || !ends_with_ci(c->path, ".exe")) continue;
		mz_zip_reader_extract_iter_state *it = mz_zip_reader_extract_iter_new(&g_zips[c->zip].za, c->entry, 0);
		if (!it) continue;
		const size_t n = mz_zip_reader_extract_iter_read(it, buf, INSTALLER_PEEK);
		mz_zip_reader_extract_iter_free(it);
		const char *kind = installer_kind(buf, n);
		if (kind)
		{
			snprintf(err, (size_t)errsize, "%s in %s is %s, which the core does not open: add %s", c->path, g_zips[c->zip].name, kind, ASK_RAW);
			said = 1;
		}
	}
	free(buf);
	return said;
}

/* whether a file is a floppy's image the core reads */
static int disk_kind_of(const char *name, long size)
{
	if (size <= 0 || (unsigned long)size > MAX_DISK) return 0;
	FILE *f = fopen(name, "rb");
	if (!f) return 0;
	uint8_t *img = malloc((size_t)size);
	const int ok = img && fread(img, 1, (size_t)size, f) == (size_t)size && disk_kind(img, (size_t)size);
	fclose(f);
	free(img);
	return ok;
}

/* a release's file that is an installer, or a PC CD's image, is refused,
 * naming the files to take out of it; 1 when it is neither */
static int refuse_container(const char *name, char *err, int errsize)
{
	uint8_t *buf = malloc(INSTALLER_PEEK);
	const long n = buf ? peek(name, buf, INSTALLER_PEEK) : -1;
	const char *kind = n > 0 ? installer_kind(buf, (size_t)(n < (long)INSTALLER_PEEK ? n : (long)INSTALLER_PEEK)) : NULL;
	int ok = 1;
	if (kind)
	{
		snprintf(err, (size_t)errsize, "%s is %s, which the core does not open: add %s", name, kind, ASK_RAW);
		ok = 0;
	}
	else if (n >= 0x8000 + 8 && is_iso9660_pvd(buf + 0x8000))
	{
		snprintf(err, (size_t)errsize, "%s is a PC CD's image, not the 3DO's disc: %s", name, ASK_PC_CD);
		ok = 0;
	}
	free(buf);
	return ok;
}

static void release_sources(void)
{
	for (int z = 0; z < g_nzips; z++)
	{
		mz_zip_reader_end(&g_zips[z].za);
		fclose(g_zips[z].f);
	}
	g_nzips = 0;
	g_nloose = 0;
	for (int i = 0; i < g_ncands; i++) free(g_cands[i].data);
	free(g_cands);
	g_cands = NULL;
	g_ncands = g_capcands = 0;
}

/* the game's folder, out of the candidates, into sealed memory */
static int settle(char *err, int errsize)
{
	char root[256] = "";
	int best = 1 << 30, found = 0;
	for (int i = 0; i < g_ncands; i++)
	{
		const int r = marker_root(g_cands[i].path);
		if (r < 0) continue;
		char folder[256];
		snprintf(folder, sizeof folder, "%.*s", r, g_cands[i].path);
		const int d = depth(folder);
		if (d < best)
		{
			best = d;
			found = 1;
			snprintf(root, sizeof root, "%s", folder);
		}
	}
	if (!found)
	{
		if (!said_installer(err, errsize))
			snprintf(err, (size_t)errsize, "the files hold no Another World data: none has what rawgl knows a release by - MEMLIST.BIN (DOS), BANK01 (Amiga, Atari ST), AW.TOS (the Atari ST demo), Data/Pak01.pak (the 15th Anniversary Edition), game/DAT/FILE017.DAT (the 20th), BANK (Windows 3.1), GameData/ (3DO)");
		return 0;
	}
	const size_t rootlen = strlen(root);
	/* the 15th's files zipped without their folders */
	const int flat15 = has_cand(root, "pak01.pak") && !has_cand(root, "data/pak01.pak");
	g_files = calloc((size_t)g_ncands, sizeof *g_files);
	for (int i = 0; i < g_ncands; i++)
	{
		const struct cand *c = &g_cands[i];
		if (strncmp(c->path, root, rootlen) != 0 || skipped(c->path + rootlen)) continue;
		uint8_t *data = alloc_sealed(c->size ? c->size : 1);
		int ok = data != NULL;
		if (ok)
		{
			if (c->zip >= 0) ok = mz_zip_reader_extract_to_mem(&g_zips[c->zip].za, c->entry, data, c->size, 0);
			else if (c->zip == -1) memcpy(data, c->data, c->size);
			else
			{
				FILE *f = fopen(g_loose[-2 - c->zip], "rb");
				ok = f && fread(data, 1, c->size, f) == c->size;
				if (f) fclose(f);
			}
		}
		if (!ok)
		{
			snprintf(err, (size_t)errsize, "%s could not be unpacked%s", c->path, data ? "" : " (out of memory)");
			return 0;
		}
		struct gfile *g = &g_files[g_nfiles++];
		const char *rel = c->path + rootlen;
		char place[256];
		if (!(flat15 && !strchr(rel, '/') && flat_place(rel, place, sizeof place))) snprintf(place, sizeof place, "%s", rel);
		snprintf(g->path, sizeof g->path, "%s", place);
		g->data = data;
		g->size = c->size;
		/* a disc image in a zip is the data path (rawgl opens it as one) */
		if (ends_with_ci(g->path, ".iso") && !strchr(g->path, '/') && g->size >= 128 && is_opera_image(g->data))
			snprintf(g_data_dir, sizeof g_data_dir, "%s", g->path);
	}
	return 1;
}

/* a disc - the 3DO's .chd or .iso - read in place */
static int open_disc(const char *name, const uint8_t *head, long size, char *err, int errsize)
{
	snprintf(g_zip_name, sizeof g_zip_name, "%.255s", name);
	snprintf(g_host_name, sizeof g_host_name, "%.255s", name);
	if (!memcmp(head, "MComprHD", 8))
	{
		/* a compressed disc: the engine is told its data track is an .iso */
		if (!open_chd(err, errsize)) return 0;
		uint8_t sector0[128];
		if (chd_read_sectors(0, sector0, sizeof sector0) != sizeof sector0 || !is_opera_image(sector0))
		{
			uint8_t pvd[8];
			if (chd_read_sectors(16 * 2048, pvd, sizeof pvd) == sizeof pvd && is_iso9660_pvd(pvd))
				snprintf(err, (size_t)errsize, "%s is a PC CD, not the 3DO's disc: %s", name, ASK_PC_CD);
			else
				snprintf(err, (size_t)errsize, "%s is a CD, but not a 3DO disc (its first sector is not an Opera file system's)", name);
			return 0;
		}
		g_has_host = 2;
	}
	else
	{
		g_host_size = (uint32_t)size;
		g_has_host = 1;
	}
	snprintf(g_host_path, sizeof g_host_path, "game.iso");
	snprintf(g_data_dir, sizeof g_data_dir, "%s", g_host_path);
	return 1;
}

int rawgl_files_load(char *err, int errsize)
{
	/* the release the project picked, and its files (firmware, under their ids) */
	char release[16] = "dos", language[8] = "us";
	wbx_setting_str("release", release, (int)sizeof release);
	wbx_setting_str("language", language, (int)sizeof language);
	const char *label = NULL;
	for (size_t i = 0; i < sizeof k_releases / sizeof k_releases[0]; i++)
		if (!strcmp(release, k_releases[i].id)) label = k_releases[i].label;
	if (!label)
	{
		snprintf(err, (size_t)errsize, "the setting release is \"%.15s\", which is none of Another World's releases", release);
		return 0;
	}
	char names[24][256];
	int count = 0, present = 0, missing = -1, firmware = 1;
	for (size_t i = 0; i < sizeof k_release_files / sizeof k_release_files[0] && count < 24; i++)
	{
		if (strcmp(k_release_files[i].release, release)) continue;
		if (k_release_files[i].language && strcmp(k_release_files[i].language, language)) continue;
		snprintf(names[count], sizeof names[0], "%s", k_release_files[i].id);
		uint8_t h[4];
		if (peek(names[count], h, sizeof h) >= 0) present++;
		else if (missing < 0) missing = count;
		count++;
	}
	if (present && missing >= 0)
	{
		snprintf(err, (size_t)errsize, "%s needs its file %s (firmware), which is not there", label, names[missing]);
		return 0;
	}
	if (!present)
	{
		/* no project's release files: a host that is no project's - chimera-run
		 * given a rom mounts it as "rom", the core's harnesses as game.zip,
		 * game.iso or game.chd, or name several in a "game" slot */
		static const char *const fallbacks[] = { "rom", "game.zip", "game.iso", "game.chd" };
		const int needed = count;
		firmware = 0;
		count = 0;
		while (count < 8 && wbx_slot_name("game", count, names[count], (int)sizeof names[0])) count++;
		if (!count)
			for (size_t i = 0; i < sizeof fallbacks / sizeof fallbacks[0]; i++)
			{
				uint8_t h[4];
				if (peek(fallbacks[i], h, sizeof h) >= 0)
				{
					snprintf(names[0], sizeof names[0], "%s", fallbacks[i]);
					count = 1;
					break;
				}
			}
		if (!count)
		{
			char list[512] = "";
			for (size_t i = 0, n = 0; i < sizeof k_release_files / sizeof k_release_files[0]; i++)
			{
				if (strcmp(k_release_files[i].release, release)) continue;
				if (k_release_files[i].language && strcmp(k_release_files[i].language, language)) continue;
				const size_t at = strlen(list);
				snprintf(list + at, sizeof list - at, "%s%s", n++ ? ", " : "", k_release_files[i].id);
			}
			snprintf(err, (size_t)errsize, "%s needs its files (firmware): %s%s", label, list, needed ? "" : " - none");
			return 0;
		}
	}

	int ok = 1;
	for (int i = 0; i < count && ok; i++)
	{
		uint8_t head[128];
		char place[256];
		const long size = peek(names[i], head, sizeof head);
		snprintf(g_zip_name, sizeof g_zip_name, "%.255s", names[i]);
		if (size < 0)
		{
			snprintf(err, (size_t)errsize, "Another World needs the game's files: %s, one of the project's, is not there", names[i]);
			ok = 0;
		}
		else if ((size >= 16 && !memcmp(head, "MComprHD", 8)) || (size >= 128 && is_opera_image(head)))
		{
			if (count > 1)
			{
				snprintf(err, (size_t)errsize, "%s is the 3DO's disc, which comes alone: the project has %d files", names[i], count);
				ok = 0;
			}
			else ok = open_disc(names[i], head, size, err, errsize);
			release_sources();
			return ok;
		}
		else if (size >= 4 && head[0] == 'P' && head[1] == 'K' && (head[2] == 3 || head[2] == 5))
			ok = add_zip(names[i], size, err, errsize);
		else if (!firmware && raw_place(names[i], head, size, place, sizeof place))
			ok = add_loose(names[i], place, size, err, errsize);
		else if (firmware && size > (long)MAX_DISK)
		{
			/* a release's file too large for a floppy: a file of the game's, unless
			 * it is an installer or a PC CD's image */
			ok = refuse_container(names[i], err, errsize)
				&& add_loose(names[i], fw_place(names[i], head, size, place, sizeof place), size, err, errsize);
		}
		else if (firmware && !disk_kind_of(names[i], size))
			ok = refuse_container(names[i], err, errsize)
				&& add_loose(names[i], fw_place(names[i], head, size, place, sizeof place), size, err, errsize);
		else if (!add_disk(names[i], size, err, errsize))
		{
			if (!err[0])
			{
				/* an installer, or a PC CD's image: the files they hold are asked for */
				uint8_t *buf = malloc(INSTALLER_PEEK);
				const long n = buf ? peek(names[i], buf, INSTALLER_PEEK) : -1;
				const char *kind = n > 0 ? installer_kind(buf, (size_t)(n < (long)INSTALLER_PEEK ? n : (long)INSTALLER_PEEK)) : NULL;
				if (kind)
					snprintf(err, (size_t)errsize, "%s is %s, which the core does not open: add %s", names[i], kind, ASK_RAW);
				else if (n >= 0x8000 + 8 && is_iso9660_pvd(buf + 0x8000))
					snprintf(err, (size_t)errsize, "%s is a PC CD's image, not the 3DO's disc: %s", names[i], ASK_PC_CD);
				else
					snprintf(err, (size_t)errsize, "%s is not a zip, a disk image the core reads (DOS or Atari ST FAT, .msa, .stx, the Amiga's .adf), a file of the 15th Anniversary Edition's (Pak01.pak, .ogg, lang_*.Txt) or the 3DO's disc (.chd, .iso)", names[i]);
				free(buf);
			}
			ok = 0;
		}
	}
	if (ok) ok = settle(err, errsize);
	release_sources();
	return ok;
}

/* the SoundFont Windows 3.1's MIDI music is played with: the firmware the
 * soundFont setting asks for, mounted under its id */
FILE *rawgl_files_soundfont(void)
{
	return fopen("soundfont.sf2", "rb");
}

int rawgl_files_count(void) { return g_nfiles + (g_has_host != 0); }

const char *rawgl_files_data_dir(void) { return g_data_dir; }

/* ------------------------------------------------------------ the engine's opens */

/* the engine asks for "<datapath>/<name>", its data path being "." - so
 * "./bank01", "./Data/Pak01.pak": the leading "./" and any "." folders go, and
 * the rest is matched without regard to case */
static void normalise(const char *path, char *want, size_t cap)
{
	size_t w = 0;
	while (*path && w < cap - 1)
	{
		if (path[0] == '.' && path[1] == '/') { path += 2; continue; }
		if (path[0] == '.' && path[1] == 0 && (w == 0 || want[w - 1] == '/')) { path++; continue; }
		if (path[0] == '/' && (w == 0 || want[w - 1] == '/')) { path++; continue; }
		want[w++] = *path++;
	}
	while (w > 0 && want[w - 1] == '/') w--;
	want[w] = 0;
}

const uint8_t *rawgl_memfs_find(const char *path, uint32_t *size)
{
	char want[256];
	normalise(path, want, sizeof want);
	for (int i = 0; i < g_nfiles; i++)
		if (!strcasecmp(g_files[i].path, want))
		{
			*size = g_files[i].size;
			return g_files[i].data;
		}
	return NULL;
}

int rawgl_memfs_host(const char *path, uint32_t *size)
{
	char want[256];
	normalise(path, want, sizeof want);
	if (!g_has_host || strcasecmp(want, g_host_path)) return -1;
	*size = g_host_size;
	return 0;
}

uint32_t rawgl_memfs_host_read(int id, uint32_t pos, void *ptr, uint32_t len)
{
	if (id != 0 || !g_has_host) return 0;
	if (g_has_host == 2) return chd_read_sectors(pos, ptr, len);
	uint8_t *out = ptr;
	uint32_t done = 0;
	while (done < len)
	{
		const uint32_t at = pos + done;
		if (at < g_host_cache_pos || at >= g_host_cache_pos + g_host_cache_len)
		{
			/* the block holding it, opened, read and closed within this call */
			g_host_cache_pos = at - at % HOST_CACHE;
			g_host_cache_len = 0;
			FILE *f = fopen(g_host_name, "rb");
			if (!f) break;
			if (fseek(f, (long)g_host_cache_pos, SEEK_SET) == 0)
				g_host_cache_len = (uint32_t)fread(g_host_cache, 1, HOST_CACHE, f);
			fclose(f);
			if (at >= g_host_cache_pos + g_host_cache_len) break;
		}
		uint32_t n = g_host_cache_pos + g_host_cache_len - at;
		if (n > len - done) n = len - done;
		memcpy(out + done, g_host_cache + (at - g_host_cache_pos), n);
		done += n;
	}
	return done;
}

/* patches/0001: rawgl's stat() - a file of the game's is a regular file, a
 * folder any of them is in is a folder, and the rest is not there */
int rawgl_memfs_stat(const char *path, struct stat *st)
{
	char want[256];
	normalise(path, want, sizeof want);
	memset(st, 0, sizeof *st);
	uint32_t size;
	if (rawgl_memfs_find(want, &size) || rawgl_memfs_host(want, &size) == 0)
	{
		st->st_mode = S_IFREG | 0444;
		st->st_size = size;
		return 0;
	}
	const size_t n = strlen(want);
	for (int i = 0; i < g_nfiles; i++)
		if (n == 0 || (!strncasecmp(g_files[i].path, want, n) && g_files[i].path[n] == '/'))
		{
			st->st_mode = S_IFDIR | 0555;
			return 0;
		}
	return -1;
}
