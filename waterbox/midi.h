/* midi.h - MIDI music, rendered with TinySoundFont (midi.c). */
#ifndef RAWGL_MIDI_H
#define RAWGL_MIDI_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct midi_song midi_song;

/* at Init only: the samples go to sealed memory. 0 when it is not a SoundFont */
int midi_load_soundfont(FILE *file);
int midi_has_soundfont(void);
/* NULL without a SoundFont, or when it is not a MIDI file */
midi_song *midi_open(const void *data, int size);
void midi_rewind(midi_song *s);
void midi_close(midi_song *s);
int midi_finished(const midi_song *s);
/* writes `frames` stereo frames at 44100 Hz */
void midi_render(midi_song *s, int16_t *stereo, int frames);

#ifdef __cplusplus
}
#endif

#endif
