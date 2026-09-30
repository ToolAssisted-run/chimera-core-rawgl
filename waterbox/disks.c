/* disks.c - the files on a floppy disk's image, for the releases that came on
 * floppies: the DOS PC's (a raw FAT image, .img/.ima), the Atari ST's (.st, a
 * raw FAT image too; .msa, its tracks run-length packed; .stx, Pasti's record
 * of the tracks, copy protection and all) and the Amiga's (.adf, an AmigaDOS
 * disk, OFS or FFS). Each is turned into its files (disks.h), which files.c
 * merges with the other disks' into the game's folder.
 *
 * Only the files are wanted, never the disk's protection: an ST disk's
 * protected tracks lie past its file system, and a Pasti track's odd sectors
 * (other sizes, other track numbers, no data) are left out. Everything here
 * reads a buffer the caller holds and allocates only the files it hands on.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disks.h"

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)((uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]); }

/* ------------------------------------------------------------ FAT (DOS, Atari ST) */

struct fat
{
	const uint8_t *img;
	size_t size;
	uint32_t bps, spc, rsv, nfat, nroot, spf, total;
	uint32_t root_off, data_off, clusters;
	int fat16;
};

/* a boot sector whose BIOS parameter block makes sense for this image - the
 * Atari ST's has no 0x55AA and often no jump, so the numbers are the test */
static int fat_open(struct fat *f, const uint8_t *img, size_t size)
{
	if (size < 512 * 4) return 0;
	f->img = img;
	f->size = size;
	f->bps = le16(img + 11);
	f->spc = img[13];
	f->rsv = le16(img + 14);
	f->nfat = img[16];
	f->nroot = le16(img + 17);
	f->total = le16(img + 19);
	if (!f->total) f->total = le32(img + 32);
	f->spf = le16(img + 22);
	if (f->bps != 512 || !f->spc || (f->spc & (f->spc - 1)) || !f->rsv || f->nfat < 1 || f->nfat > 2 || !f->nroot || !f->spf)
		return 0;
	if ((size_t)f->total * f->bps > size + 512 * 18 * 2 || f->total < 16) return 0;
	f->root_off = (f->rsv + f->nfat * f->spf) * f->bps;
	f->data_off = f->root_off + (f->nroot * 32 + f->bps - 1) / f->bps * f->bps;
	if (f->data_off >= size) return 0;
	f->clusters = (f->total * f->bps - f->data_off) / (f->spc * f->bps);
	f->fat16 = f->clusters >= 4085;
	return 1;
}

static uint32_t fat_next(const struct fat *f, uint32_t c)
{
	const uint8_t *t = f->img + f->rsv * f->bps;
	if (f->fat16) return le16(t + c * 2);
	const uint32_t o = c * 3 / 2;
	const uint32_t v = t[o] | t[o + 1] << 8;
	return c & 1 ? v >> 4 : v & 0xFFF;
}

static int fat_end(const struct fat *f, uint32_t c) { return c < 2 || c >= (f->fat16 ? 0xFFF8u : 0xFF8u) || c - 2 >= f->clusters; }

/* a cluster chain's bytes, up to `size` (a directory's: all of them) */
static uint8_t *fat_chain(const struct fat *f, uint32_t c, uint32_t size, uint32_t *got)
{
	const uint32_t cb = f->spc * f->bps;
	uint8_t *out = malloc(size ? size : 1);
	uint32_t n = 0, guard = 0;
	while (!fat_end(f, c) && n < size && guard++ < 65536)
	{
		const size_t off = f->data_off + (size_t)(c - 2) * cb;
		if (off + cb > f->size) break;
		const uint32_t k = size - n < cb ? size - n : cb;
		memcpy(out + n, f->img + off, k);
		n += k;
		c = fat_next(f, c);
	}
	*got = n;
	return out;
}

static void fat_dir(const struct fat *f, const uint8_t *d, uint32_t len, const char *prefix, int depth, disk_file_fn fn, void *ctx)
{
	for (uint32_t i = 0; i + 32 <= len; i += 32)
	{
		const uint8_t *e = d + i;
		if (e[0] == 0) break;
		if (e[0] == 0xE5 || e[11] == 0x0F || (e[11] & 0x08)) continue;   /* deleted, a long name, the label */
		char name[13];
		int n = 0;
		for (int k = 0; k < 8 && e[k] != ' '; k++) name[n++] = (char)e[k];
		if (e[8] != ' ')
		{
			name[n++] = '.';
			for (int k = 8; k < 11 && e[k] != ' '; k++) name[n++] = (char)e[k];
		}
		name[n] = 0;
		if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
		char path[256];
		snprintf(path, sizeof path, "%s%s", prefix, name);
		const uint32_t first = le16(e + 26);
		if (e[11] & 0x10)
		{
			if (depth > 8) continue;
			uint32_t got;
			uint8_t *sub = fat_chain(f, first, 1u << 20, &got);
			char p2[260];
			snprintf(p2, sizeof p2, "%s/", path);
			fat_dir(f, sub, got, p2, depth + 1, fn, ctx);
			free(sub);
		}
		else
		{
			const uint32_t size = le32(e + 28);
			uint32_t got = 0;
			uint8_t *data = size ? fat_chain(f, first, size, &got) : malloc(1);
			if (got == size) fn(ctx, path, data, size);
			free(data);
		}
	}
}

static int fat_read(const uint8_t *img, size_t size, disk_file_fn fn, void *ctx)
{
	struct fat f;
	if (!fat_open(&f, img, size)) return 0;
	const uint32_t len = f.nroot * 32;
	if (f.root_off + len > size) return 0;
	fat_dir(&f, img + f.root_off, len, "", 0, fn, ctx);
	return 1;
}

/* ------------------------------------------------------------ Atari ST: MSA, Pasti */

/* MSA: 0x0E0F, sectors a track, sides - 1, first and last track; then each
 * track and side: its length, and 512 * sectors bytes as they are, or packed
 * (0xE5, the byte, a 16-bit count: a run) */
static uint8_t *msa_to_raw(const uint8_t *m, size_t size, size_t *out_size)
{
	if (size < 10 || be16(m) != 0x0E0F) return NULL;
	const uint32_t spt = be16(m + 2), sides = be16(m + 4) + 1u, first = be16(m + 6), last = be16(m + 8);
	if (!spt || spt > 24 || sides > 2 || last < first || last > 90) return NULL;
	const uint32_t tlen = spt * 512;
	const size_t raw_size = (size_t)(last + 1) * sides * tlen;
	uint8_t *raw = calloc(1, raw_size);
	size_t p = 10;
	for (uint32_t t = first; t <= last; t++)
		for (uint32_t s = 0; s < sides; s++)
		{
			if (p + 2 > size) goto bad;
			const uint32_t len = be16(m + p);
			p += 2;
			if (p + len > size) goto bad;
			uint8_t *dst = raw + ((size_t)t * sides + s) * tlen;
			if (len == tlen) memcpy(dst, m + p, tlen);
			else
			{
				uint32_t o = 0;
				for (uint32_t i = 0; i < len && o < tlen;)
				{
					if (m[p + i] == 0xE5 && i + 3 < len)
					{
						const uint8_t v = m[p + i + 1];
						uint32_t n = be16(m + p + i + 2);
						if (n > tlen - o) n = tlen - o;
						memset(dst + o, v, n);
						o += n;
						i += 4;
					}
					else dst[o++] = m[p + i++];
				}
			}
			p += len;
		}
	*out_size = raw_size;
	return raw;
bad:
	free(raw);
	return NULL;
}

/* Pasti (.stx), as Hatari's stx.c reads it: "RSY\0", then a record a track -
 * its sectors described (their place in the track's data, their ID field)
 * or, without descriptors, 512-byte sectors one after another. The ordinary
 * sectors (512 bytes, the track's own number, with data) make a raw image of
 * the geometry the boot sector gives. */
static uint8_t *stx_to_raw(const uint8_t *d, size_t size, size_t *out_size)
{
	if (size < 16 || memcmp(d, "RSY\0", 4)) return NULL;
	const uint32_t tracks = d[10];
	enum { MAXC = 90, MAXS = 24 };
	const uint8_t *sec[MAXC][2][MAXS + 1];
	memset(sec, 0, sizeof sec);
	size_t pos = 16;
	for (uint32_t t = 0; t < tracks; t++)
	{
		if (pos + 16 > size) return NULL;
		const uint32_t rsize = le32(d + pos), fuzzy = le32(d + pos + 4);
		const uint32_t count = le16(d + pos + 8), flags = le16(d + pos + 10);
		const uint32_t tnum = d[pos + 14];
		const uint32_t track = tnum & 0x7F, side = tnum >> 7;
		const size_t base = pos + 16;
		if (rsize < 16 || pos + rsize > size) return NULL;
		if (track < MAXC)
		{
			if (flags & 0x01)
			{
				const size_t data = base + (size_t)count * 16 + fuzzy;
				for (uint32_t i = 0; i < count; i++)
				{
					const uint8_t *e = d + base + i * 16;
					const uint32_t off = le32(e), idt = e[8], num = e[10], sz = e[11] & 3, fdc = e[14];
					if ((fdc & 0x10) || sz != 2 || idt != track || num < 1 || num > MAXS) continue;
					if (data + off + 512 > pos + rsize) continue;
					if (!sec[track][side][num]) sec[track][side][num] = d + data + off;
				}
			}
			else
				for (uint32_t i = 0; i < count && i < MAXS; i++)
					if (base + (i + 1) * 512 <= pos + rsize) sec[track][side][i + 1] = d + base + i * 512;
		}
		pos += rsize;
	}
	const uint8_t *boot = sec[0][0][1];
	if (!boot) return NULL;
	const uint32_t total = le16(boot + 19), spt = le16(boot + 24), heads = le16(boot + 26);
	if (!spt || spt > MAXS || !heads || heads > 2 || !total) return NULL;
	const uint32_t cyls = (total + spt * heads - 1) / (spt * heads);
	if (cyls > MAXC) return NULL;
	uint8_t *raw = calloc(1, (size_t)total * 512);
	for (uint32_t c = 0; c < cyls; c++)
		for (uint32_t h = 0; h < heads; h++)
			for (uint32_t s = 1; s <= spt; s++)
			{
				const size_t o = ((size_t)(c * heads + h) * spt + (s - 1)) * 512;
				if (o + 512 <= (size_t)total * 512 && sec[c][h][s]) memcpy(raw + o, sec[c][h][s], 512);
			}
	*out_size = (size_t)total * 512;
	return raw;
}

/* ------------------------------------------------------------ Amiga: AmigaDOS */

struct adf
{
	const uint8_t *img;
	size_t size;
	uint32_t blocks;
	int ffs;
};

static const uint8_t *adf_block(const struct adf *a, uint32_t n) { return n < a->blocks ? a->img + (size_t)n * 512 : NULL; }

static void adf_name(const uint8_t *b, char *out)
{
	uint32_t n = b[512 - 80];
	if (n > 30) n = 30;
	memcpy(out, b + 512 - 79, n);
	out[n] = 0;
}

static int adf_file(const struct adf *a, const uint8_t *hdr, uint8_t **data_out, uint32_t *size_out)
{
	const uint32_t size = be32(hdr + 512 - 188);
	uint8_t *data = malloc(size ? size : 1);
	uint32_t n = 0, guard = 0;
	const uint8_t *h = hdr;
	while (h && n < size && guard++ < 4096)
	{
		const uint32_t count = be32(h + 8);
		for (uint32_t k = 0; k < count && k < 72 && n < size; k++)
		{
			const uint8_t *db = adf_block(a, be32(h + 512 - 204 - k * 4));
			if (!db) break;
			uint32_t len = 512, off = 0;
			if (!a->ffs)
			{
				off = 24;
				len = be32(db + 12);
				if (len > 488) len = 488;
			}
			if (len > size - n) len = size - n;
			memcpy(data + n, db + off, len);
			n += len;
		}
		const uint32_t ext = be32(h + 512 - 8);
		h = ext ? adf_block(a, ext) : NULL;
	}
	if (n != size)
	{
		free(data);
		return 0;
	}
	*data_out = data;
	*size_out = size;
	return 1;
}

static void adf_dir(const struct adf *a, const uint8_t *dir, const char *prefix, int depth, disk_file_fn fn, void *ctx)
{
	for (int i = 0; i < 72; i++)
	{
		uint32_t h = be32(dir + 24 + i * 4), guard = 0;
		while (h && guard++ < 1024)
		{
			const uint8_t *e = adf_block(a, h);
			if (!e || be32(e) != 2) break;
			const int32_t sec = (int32_t)be32(e + 512 - 4);
			char name[32], path[256];
			adf_name(e, name);
			snprintf(path, sizeof path, "%s%s", prefix, name);
			if (sec == 2 && depth < 8)
			{
				char p2[260];
				snprintf(p2, sizeof p2, "%s/", path);
				adf_dir(a, e, p2, depth + 1, fn, ctx);
			}
			else if (sec == -3)
			{
				uint8_t *data;
				uint32_t size;
				if (adf_file(a, e, &data, &size))
				{
					fn(ctx, path, data, size);
					free(data);
				}
			}
			h = be32(e + 512 - 16);
		}
	}
}

static int adf_read(const uint8_t *img, size_t size, disk_file_fn fn, void *ctx)
{
	if ((size != 901120 && size != 1802240) || memcmp(img, "DOS", 3) || img[3] > 7) return 0;
	struct adf a = { img, size, (uint32_t)(size / 512), img[3] & 1 };
	const uint8_t *root = adf_block(&a, a.blocks / 2);
	if (!root || be32(root) != 2 || (int32_t)be32(root + 508) != 1) return 0;
	adf_dir(&a, root, "", 0, fn, ctx);
	return 1;
}

/* ------------------------------------------------------------ any of them */

const char *disk_kind(const uint8_t *img, size_t size)
{
	if (size >= 4 && !memcmp(img, "RSY\0", 4)) return "Atari ST Pasti";
	if (size >= 10 && be16(img) == 0x0E0F) return "Atari ST MSA";
	if ((size == 901120 || size == 1802240) && !memcmp(img, "DOS", 3)) return "Amiga";
	struct fat f;
	if (fat_open(&f, img, size)) return "FAT";
	return NULL;
}

int disk_read(const uint8_t *img, size_t size, disk_file_fn fn, void *ctx)
{
	const char *kind = disk_kind(img, size);
	if (!kind) return 0;
	if (!strcmp(kind, "Amiga")) return adf_read(img, size, fn, ctx);
	if (!strcmp(kind, "FAT")) return fat_read(img, size, fn, ctx);
	size_t raw_size = 0;
	uint8_t *raw = !strcmp(kind, "Atari ST MSA") ? msa_to_raw(img, size, &raw_size) : stx_to_raw(img, size, &raw_size);
	if (!raw) return 0;
	const int ok = fat_read(raw, raw_size, fn, ctx);
	free(raw);
	return ok;
}
