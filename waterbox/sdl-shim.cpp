/* sdl-shim.cpp - the SDL_mixer rawgl's mixer.cpp plays through, as the core's.
 *
 * Upstream plays through SDL_mixer, whose audio thread asks for sound as
 * wall-clock time passes. Here the driver asks (rawgl_audio_mix) for exactly
 * the samples each sleep of the game covers, on the game's own stack, between
 * two instructions of its loop - so the sound, and everything the script
 * learns from it (the music player's VAR_MUSIC_SYNC), are a function of the
 * inputs. What rawgl uses of SDL_mixer, per release:
 *
 *   - DOS, Amiga, Atari ST: a music hook (Mix_HookMusic) - rawgl's own mixer,
 *     its four channels and its module player.
 *   - 15th and 20th Anniversary Editions, Windows 3.1: a post-mix callback
 *     (Mix_SetPostMix), rawgl's WAV channels, and SDL_mixer's music: a WAV
 *     (15th), an Ogg Vorbis (20th, vorbis.c) or a MIDI file (Windows 3.1,
 *     midi.c, with the SoundFont firmware).
 *   - 3DO: SDL_mixer's channels playing AIFF sounds rawgl preloads
 *     (Mix_LoadWAV_RW, Mix_PlayChannel), and a music hook for its AIFF-C songs.
 *
 * Mixed as SDL_mixer mixes: the music (the hook, or the playing music at its
 * volume) first, then the channels at theirs, then the post-mix callback; a
 * sum is clipped to 16 bits. Rates other than 44100 Hz are resampled linearly,
 * in integers. Everything here is guest memory, so a savestate carries it.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compat/SDL_mixer.h"
#include "midi.h"
#include "rawgl-audio.h"
#include "rawgl-files.h"

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

typedef void (*mix_fn)(void *udata, uint8_t *stream, int len);

static const int kRate = 44100;

static inline int16_t clip16(int v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

/* ------------------------------------------------------------ resampling */

/* linear, in 32.32 fixed point: the output frame lies `frac` of the way from
 * cur to next, source frames */
struct Resampler
{
	uint64_t step;
	uint32_t frac;
	int16_t cur[2], next[2];
	int primed;
	void reset(int rate)
	{
		step = ((uint64_t)(uint32_t)rate << 32) / (uint32_t)kRate;
		frac = 0;
		cur[0] = cur[1] = next[0] = next[1] = 0;
		primed = 0;
	}
};

/* a source gives stereo frames at its own rate; 0 when it has no more */
struct Source
{
	virtual ~Source() {}
	virtual int read(int16_t *stereo, int frames) = 0;
	virtual void rewind() = 0;
};

/* `frames` output frames from src through rs; returns how many there were */
static int resample(Source *src, Resampler *rs, int16_t *out, int frames)
{
	if (!rs->primed)
	{
		if (src->read(rs->cur, 1) != 1) return 0;
		if (src->read(rs->next, 1) != 1) memcpy(rs->next, rs->cur, sizeof rs->next);
		rs->primed = 1;
	}
	int n = 0;
	for (; n < frames; n++)
	{
		for (int c = 0; c < 2; c++)
			out[n * 2 + c] = (int16_t)(rs->cur[c] + (int)(((int64_t)(rs->next[c] - rs->cur[c]) * rs->frac) >> 32));
		const uint64_t acc = (uint64_t)rs->frac + rs->step;
		rs->frac = (uint32_t)acc;
		for (uint64_t k = acc >> 32; k > 0; k--)
		{
			memcpy(rs->cur, rs->next, sizeof rs->cur);
			if (src->read(rs->next, 1) != 1)
			{
				rs->primed = 0;
				return n + 1;
			}
		}
	}
	return n;
}

/* ------------------------------------------------------------ PCM: WAV and AIFF */

struct Pcm
{
	const uint8_t *data;
	uint32_t frames;
	int channels, bits, rate;
	bool big_endian, is_signed;
};

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)((uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]); }

/* RIFF WAVE, PCM 8 or 16 bits, mono or stereo */
static bool parse_wav(const uint8_t *d, uint32_t size, Pcm *pcm)
{
	if (size < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return false;
	bool fmt = false;
	for (uint32_t off = 12; off + 8 <= size;)
	{
		const uint32_t len = le32(d + off + 4);
		const uint8_t *c = d + off + 8;
		if (!memcmp(d + off, "fmt ", 4) && len >= 16 && off + 8 + 16 <= size)
		{
			if (le16(c) != 1) return false;
			pcm->channels = le16(c + 2);
			pcm->rate = (int)le32(c + 4);
			pcm->bits = le16(c + 14);
			fmt = true;
		}
		else if (!memcmp(d + off, "data", 4) && fmt)
		{
			uint32_t n = len;
			if (off + 8 + n > size) n = size - off - 8;
			if ((pcm->channels != 1 && pcm->channels != 2) || (pcm->bits != 8 && pcm->bits != 16) || pcm->rate <= 0) return false;
			pcm->data = c;
			pcm->frames = n / (uint32_t)(pcm->channels * pcm->bits / 8);
			pcm->big_endian = false;
			pcm->is_signed = pcm->bits == 16;
			return true;
		}
		off += 8 + len + (len & 1);
	}
	return false;
}

/* AIFF and AIFF-C (uncompressed: NONE, big-endian, or sowt, little-endian),
 * 8 or 16 bits, mono or stereo - what SDL_mixer's loader reads */
static bool parse_aiff(const uint8_t *d, uint32_t size, Pcm *pcm)
{
	if (size < 12 || memcmp(d, "FORM", 4) || (memcmp(d + 8, "AIFF", 4) && memcmp(d + 8, "AIFC", 4))) return false;
	const bool aifc = !memcmp(d + 8, "AIFC", 4);
	bool comm = false;
	pcm->big_endian = true;
	for (uint32_t off = 12; off + 8 <= size;)
	{
		const uint32_t len = be32(d + off + 4);
		const uint8_t *c = d + off + 8;
		if (!memcmp(d + off, "COMM", 4) && len >= 18 && off + 8 + 18 <= size)
		{
			pcm->channels = be16(c);
			pcm->frames = be32(c + 2);
			pcm->bits = be16(c + 6);
			/* the rate, an 80-bit extended float, as rawgl's aifcplayer.cpp reads it */
			const int e = 30 - c[9];
			pcm->rate = (e >= 0 && e < 32) ? (int)(be32(c + 10) >> e) : 0;
			if (aifc && len >= 22)
			{
				if (!memcmp(c + 18, "sowt", 4)) pcm->big_endian = false;
				else if (memcmp(c + 18, "NONE", 4)) return false;
			}
			comm = true;
		}
		else if (!memcmp(d + off, "SSND", 4) && comm && len >= 8)
		{
			const uint32_t skip = be32(c);
			const uint32_t avail = (off + 8 + len <= size ? len : size - off - 8);
			if (8 + skip > avail) return false;
			if ((pcm->channels != 1 && pcm->channels != 2) || (pcm->bits != 8 && pcm->bits != 16) || pcm->rate <= 0) return false;
			const uint32_t maxf = (avail - 8 - skip) / (uint32_t)(pcm->channels * pcm->bits / 8);
			if (pcm->frames > maxf) pcm->frames = maxf;
			pcm->data = c + 8 + skip;
			pcm->is_signed = true;
			return true;
		}
		off += 8 + len + (len & 1);
	}
	return false;
}

static void pcm_frame(const Pcm *p, uint32_t i, int16_t out[2])
{
	for (int c = 0; c < 2; c++)
	{
		const uint32_t k = i * (uint32_t)p->channels + (uint32_t)(p->channels == 2 ? c : 0);
		int v;
		if (p->bits == 8)
			v = p->is_signed ? (int)(int8_t)p->data[k] << 8 : ((int)p->data[k] - 128) << 8;
		else
			v = (int16_t)(p->big_endian ? be16(p->data + k * 2) : le16(p->data + k * 2));
		out[c] = (int16_t)v;
	}
}

struct PcmSource : Source
{
	Pcm pcm;
	uint32_t pos;
	int read(int16_t *stereo, int frames) override
	{
		int n = 0;
		for (; n < frames && pos < pcm.frames; n++, pos++) pcm_frame(&pcm, pos, stereo + n * 2);
		return n;
	}
	void rewind() override { pos = 0; }
};

/* ------------------------------------------------------------ Ogg Vorbis, MIDI */

struct VorbisSource : Source
{
	stb_vorbis *v;
	int16_t buf[2 * 1024];
	int len, at;
	int read(int16_t *stereo, int frames) override
	{
		int n = 0;
		while (n < frames)
		{
			if (at >= len)
			{
				len = stb_vorbis_get_samples_short_interleaved(v, 2, buf, 2 * 1024);
				at = 0;
				if (len <= 0)
				{
					len = 0;
					break;
				}
			}
			stereo[n * 2] = buf[at * 2];
			stereo[n * 2 + 1] = buf[at * 2 + 1];
			at++;
			n++;
		}
		return n;
	}
	void rewind() override
	{
		stb_vorbis_seek_start(v);
		len = at = 0;
	}
	~VorbisSource() override { stb_vorbis_close(v); }
};

struct MidiSource : Source
{
	midi_song *song;
	int read(int16_t *stereo, int frames) override
	{
		if (midi_finished(song)) return 0;
		midi_render(song, stereo, frames);
		return frames;
	}
	void rewind() override { midi_rewind(song); }
	~MidiSource() override { midi_close(song); }
};

/* ------------------------------------------------------------ SDL_mixer's music */

struct Mix_Music
{
	Source *src;
	int rate;
};

static Mix_Music *g_playing;
static Resampler g_music_rs;
static int g_music_loops;     /* -1: for ever */
static int g_music_volume = MIX_MAX_VOLUME;

static mix_fn g_music_hook;
static void *g_music_hook_arg;
static mix_fn g_post;
static void *g_post_arg;

Mix_Music *Mix_LoadMUS(const char *file)
{
	uint32_t size = 0;
	const uint8_t *d = rawgl_memfs_find(file, &size);
	if (!d || size < 12) return 0;
	Mix_Music *m = 0;
	Pcm pcm;
	if (parse_wav(d, size, &pcm))
	{
		PcmSource *s = new PcmSource;
		s->pcm = pcm;
		s->pos = 0;
		m = new Mix_Music;
		m->src = s;
		m->rate = pcm.rate;
	}
	else if (!memcmp(d, "OggS", 4))
	{
		int error = 0;
		stb_vorbis *v = stb_vorbis_open_memory(d, (int)size, &error, 0);
		if (!v) return 0;
		const stb_vorbis_info info = stb_vorbis_get_info(v);
		VorbisSource *s = new VorbisSource;
		s->v = v;
		s->len = s->at = 0;
		m = new Mix_Music;
		m->src = s;
		m->rate = (int)info.sample_rate;
	}
	else if (!memcmp(d, "MThd", 4))
	{
		midi_song *song = midi_open(d, (int)size);
		if (!song) return 0;
		MidiSource *s = new MidiSource;
		s->song = song;
		m = new Mix_Music;
		m->src = s;
		m->rate = kRate;
	}
	return m;
}

int Mix_PlayMusic(Mix_Music *music, int loops)
{
	if (!music) return -1;
	music->src->rewind();
	g_music_rs.reset(music->rate);
	g_playing = music;
	g_music_loops = loops;
	return 0;
}

int Mix_HaltMusic(void)
{
	g_playing = 0;
	return 0;
}

void Mix_FreeMusic(Mix_Music *music)
{
	if (!music) return;
	if (g_playing == music) g_playing = 0;
	delete music->src;
	delete music;
}

int Mix_VolumeMusic(int volume)
{
	const int prev = g_music_volume;
	if (volume >= 0) g_music_volume = volume > MIX_MAX_VOLUME ? MIX_MAX_VOLUME : volume;
	return prev;
}

void Mix_HookMusic(mix_fn f, void *arg) { g_music_hook = f; g_music_hook_arg = arg; }
void Mix_SetPostMix(mix_fn f, void *arg) { g_post = f; g_post_arg = arg; }

static void mix_music(int16_t *stereo, int frames)
{
	static int16_t tmp[2 * 1024];
	while (frames > 0 && g_playing)
	{
		const int want = frames > 1024 ? 1024 : frames;
		int got = resample(g_playing->src, &g_music_rs, tmp, want);
		for (int i = 0; i < got * 2; i++) stereo[i] = clip16(stereo[i] + tmp[i] * g_music_volume / MIX_MAX_VOLUME);
		stereo += got * 2;
		frames -= got;
		if (got < want)
		{
			/* the end: again, while loops are left */
			if (g_music_loops == 0) g_playing = 0;
			else
			{
				if (g_music_loops > 0) g_music_loops--;
				g_playing->src->rewind();
				g_music_rs.reset(g_playing->rate);
			}
		}
	}
}

/* ------------------------------------------------------------ SDL_mixer's channels */

struct Mix_Chunk
{
	int16_t *data;   /* stereo, 44100 Hz */
	uint32_t frames;
};

struct SDL_RWops
{
	const uint8_t *mem;
	int size;
};

enum { kMaxChannels = 16 };

static struct
{
	Mix_Chunk *chunk;
	uint32_t pos;
	int loops;
	int volume;
} g_ch[kMaxChannels];
static int g_nch = 8;

SDL_RWops *SDL_RWFromConstMem(const void *mem, int size)
{
	SDL_RWops *rw = (SDL_RWops *)malloc(sizeof *rw);
	rw->mem = (const uint8_t *)mem;
	rw->size = size;
	return rw;
}

/* decoded and resampled once, as SDL_mixer converts a chunk to the device's
 * format when it loads it */
Mix_Chunk *Mix_LoadWAV_RW(SDL_RWops *src, int freesrc)
{
	if (!src) return 0;
	Pcm pcm;
	Mix_Chunk *chunk = 0;
	if (parse_aiff(src->mem, (uint32_t)src->size, &pcm) || parse_wav(src->mem, (uint32_t)src->size, &pcm))
	{
		PcmSource s;
		s.pcm = pcm;
		s.pos = 0;
		Resampler rs;
		rs.reset(pcm.rate);
		const uint32_t frames = (uint32_t)(((uint64_t)pcm.frames * kRate + (uint32_t)pcm.rate - 1) / (uint32_t)pcm.rate);
		chunk = (Mix_Chunk *)malloc(sizeof *chunk);
		chunk->data = (int16_t *)malloc(((size_t)frames + 1) * 4);
		chunk->frames = (uint32_t)resample(&s, &rs, chunk->data, (int)frames);
	}
	if (freesrc) free(src);
	return chunk;
}

void Mix_FreeChunk(Mix_Chunk *chunk)
{
	if (!chunk) return;
	for (int i = 0; i < kMaxChannels; i++)
		if (g_ch[i].chunk == chunk) g_ch[i].chunk = 0;
	free(chunk->data);
	free(chunk);
}

int Mix_AllocateChannels(int numchans)
{
	if (numchans >= 0) g_nch = numchans > kMaxChannels ? kMaxChannels : numchans;
	return g_nch;
}

int Mix_PlayChannel(int channel, Mix_Chunk *chunk, int loops)
{
	if (!chunk) return -1;
	if (channel < 0)
		for (int i = 0; i < g_nch && channel < 0; i++)
			if (!g_ch[i].chunk) channel = i;
	if (channel < 0 || channel >= g_nch) return -1;
	g_ch[channel].chunk = chunk;
	g_ch[channel].pos = 0;
	g_ch[channel].loops = loops;
	return channel;
}

int Mix_HaltChannel(int channel)
{
	for (int i = 0; i < kMaxChannels; i++)
		if (channel < 0 || channel == i) g_ch[i].chunk = 0;
	return 0;
}

int Mix_Playing(int channel)
{
	if (channel >= 0) return channel < kMaxChannels && g_ch[channel].chunk != 0;
	int n = 0;
	for (int i = 0; i < kMaxChannels; i++) n += g_ch[i].chunk != 0;
	return n;
}

int Mix_Volume(int channel, int volume)
{
	int prev = 0;
	for (int i = 0; i < kMaxChannels; i++)
		if (channel < 0 || channel == i)
		{
			prev = g_ch[i].volume;
			if (volume >= 0) g_ch[i].volume = volume > MIX_MAX_VOLUME ? MIX_MAX_VOLUME : volume;
		}
	return prev;
}

static void mix_channels(int16_t *stereo, int frames)
{
	for (int i = 0; i < g_nch; i++)
	{
		for (int f = 0; f < frames && g_ch[i].chunk;)
		{
			Mix_Chunk *c = g_ch[i].chunk;
			if (g_ch[i].pos >= c->frames)
			{
				if (g_ch[i].loops == 0)
				{
					g_ch[i].chunk = 0;
					break;
				}
				if (g_ch[i].loops > 0) g_ch[i].loops--;
				g_ch[i].pos = 0;
				if (!c->frames) { g_ch[i].chunk = 0; break; }
			}
			const int16_t *s = c->data + g_ch[i].pos * 2;
			stereo[f * 2] = clip16(stereo[f * 2] + s[0] * g_ch[i].volume / MIX_MAX_VOLUME);
			stereo[f * 2 + 1] = clip16(stereo[f * 2 + 1] + s[1] * g_ch[i].volume / MIX_MAX_VOLUME);
			g_ch[i].pos++;
			f++;
		}
	}
}

/* ------------------------------------------------------------ the rest */

int Mix_Init(int flags) { return flags; }
void Mix_Quit(void) {}
int Mix_OpenAudio(int frequency, SDL_AudioFormat format, int channels, int chunksize)
{
	(void)frequency; (void)format; (void)channels; (void)chunksize;
	for (int i = 0; i < kMaxChannels; i++) g_ch[i].volume = MIX_MAX_VOLUME;
	return 0;
}
void Mix_CloseAudio(void)
{
	g_music_hook = g_post = 0;
	g_music_hook_arg = g_post_arg = 0;
	g_playing = 0;
	Mix_HaltChannel(-1);
}
const char *Mix_GetError(void) { return ""; }

void rawgl_audio_mix(int16_t *stereo, int frames)
{
	const int len = frames * 2 * (int)sizeof(int16_t);
	memset(stereo, 0, (size_t)len);
	if (g_music_hook) g_music_hook(g_music_hook_arg, (uint8_t *)stereo, len);
	else mix_music(stereo, frames);
	mix_channels(stereo, frames);
	if (g_post) g_post(g_post_arg, (uint8_t *)stereo, len);
}
