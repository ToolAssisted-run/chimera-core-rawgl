/* midi.c - MIDI music for the Windows 3.1 release, with TinySoundFont.
 *
 * rawgl's Windows 3.1 path plays two MIDI files (the intro's and the end's,
 * resource_win31.cpp getMusicName) through SDL_mixer, which renders them with
 * whatever synthesizer the host has. The core renders them itself, with
 * TinySoundFont (extern/TinySoundFont) and the SoundFont the project brings
 * (firmware soundfont.sf2, with the soundFont setting): the same notes on every machine, and no
 * music at all without a SoundFont - as rawgl's SDL build without a MIDI
 * synthesizer.
 *
 * The SoundFont is loaded at Init, and its samples - TinySoundFont keeps them
 * as floats, twice the file - go to sealed memory, which no savestate carries.
 * What a song changes as it plays (voices, channels, the position) is on the
 * heap like the rest of the machine. TinySoundFont's arithmetic goes through
 * detmath.h, so both builds render the same samples.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <emulibc.h>

#include "detmath.h"
#include "midi.h"

/* allocations carry their size, so the sample buffer can be moved to sealed
 * memory after the load */
static void *midi_malloc(size_t n)
{
	size_t *p = malloc(n + 16);
	if (!p) return NULL;
	p[0] = n;
	return (uint8_t *)p + 16;
}
static void midi_free(void *q)
{
	if (q) free((uint8_t *)q - 16);
}
static void *midi_realloc(void *q, size_t n)
{
	if (!q) return midi_malloc(n);
	size_t *p = realloc((uint8_t *)q - 16, n + 16);
	if (!p) return NULL;
	p[0] = n;
	return (uint8_t *)p + 16;
}
static size_t midi_size(const void *q) { return *(const size_t *)((const uint8_t *)q - 16); }

#define TSF_MALLOC midi_malloc
#define TSF_REALLOC midi_realloc
#define TSF_FREE midi_free
#define TML_MALLOC midi_malloc
#define TML_REALLOC midi_realloc
#define TML_FREE midi_free
#define TSF_POW dm_pow
#define TSF_POWF dm_powf
#define TSF_EXPF dm_expf
#define TSF_LOG dm_log
#define TSF_TAN dm_tan
#define TSF_LOG10 dm_log10
#define TSF_SQRT sqrt
#define TSF_SQRTF sqrtf
#define TSF_NO_STDIO
#define TML_NO_STDIO
#define TSF_IMPLEMENTATION
#define TML_IMPLEMENTATION
#define TSF_STATIC
#define TML_STATIC
#include "tsf.h"
#include "tml.h"

static tsf *g_font;

static int sf_read(void *f, void *ptr, unsigned int size) { return (int)fread(ptr, 1, size, (FILE *)f); }
static int sf_skip(void *f, unsigned int count) { return !fseek((FILE *)f, (long)count, SEEK_CUR); }

int midi_load_soundfont(FILE *file)
{
	/* read as it is parsed, never whole */
	struct tsf_stream stream = { file, sf_read, sf_skip };
	tsf *f = tsf_load(&stream);
	if (!f) return 0;
	/* the samples, read-only from here on, to sealed memory */
	const size_t n = midi_size(f->fontSamples);
	float *sealed = alloc_sealed(n ? n : 1);
	if (sealed)
	{
		memcpy(sealed, f->fontSamples, n);
		midi_free(f->fontSamples);
		f->fontSamples = sealed;
	}
	tsf_set_output(f, TSF_STEREO_INTERLEAVED, 44100, 0.0f);
	g_font = f;
	return 1;
}

int midi_has_soundfont(void) { return g_font != NULL; }

struct midi_song
{
	tml_message *first, *next;
	double msec;
	int finished;
};

midi_song *midi_open(const void *data, int size)
{
	if (!g_font) return NULL;
	tml_message *m = tml_load_memory(data, size);
	if (!m) return NULL;
	midi_song *s = calloc(1, sizeof *s);
	s->first = s->next = m;
	return s;
}

void midi_rewind(midi_song *s)
{
	tsf_reset(g_font);
	s->next = s->first;
	s->msec = 0;
	s->finished = 0;
}

void midi_close(midi_song *s)
{
	if (!s) return;
	if (g_font) tsf_reset(g_font);
	tml_free(s->first);
	free(s);
}

int midi_finished(const midi_song *s) { return s->finished; }

/* TinySoundFont's example player (examples/example3.c): the messages due are
 * sent before each block of 64 frames, the position counted in milliseconds */
void midi_render(midi_song *s, int16_t *stereo, int frames)
{
	memset(stereo, 0, (size_t)frames * 4);
	while (frames > 0)
	{
		int block = TSF_RENDER_EFFECTSAMPLEBLOCK;
		if (block > frames) block = frames;
		for (s->msec += block * (1000.0 / 44100.0); s->next && s->msec >= s->next->time; s->next = s->next->next)
		{
			const tml_message *m = s->next;
			switch (m->type)
			{
			case TML_PROGRAM_CHANGE:
				tsf_channel_set_presetnumber(g_font, m->channel, m->program, m->channel == 9);
				break;
			case TML_NOTE_ON:
				tsf_channel_note_on(g_font, m->channel, m->key, m->velocity / 127.0f);
				break;
			case TML_NOTE_OFF:
				tsf_channel_note_off(g_font, m->channel, m->key);
				break;
			case TML_PITCH_BEND:
				tsf_channel_set_pitchwheel(g_font, m->channel, m->pitch_bend);
				break;
			case TML_CONTROL_CHANGE:
				tsf_channel_midi_control(g_font, m->channel, m->control, m->control_value);
				break;
			}
		}
		tsf_render_short(g_font, stereo, block, 0);
		stereo += block * 2;
		frames -= block;
	}
	/* the song is over when its last message is out and its last note has died */
	if (!s->next && tsf_active_voice_count(g_font) == 0) s->finished = 1;
}
