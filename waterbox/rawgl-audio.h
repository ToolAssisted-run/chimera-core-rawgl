/* rawgl-audio.h - the engine's mixer, run for a span of the machine's time
 * (sdl-shim.cpp). */
#ifndef RAWGL_AUDIO_H
#define RAWGL_AUDIO_H

#include <stdint.h>

/* fills `frames` stereo frames (44100 Hz) with what the engine's mixer plays
 * next, advancing its channels and its music player by as much */
void rawgl_audio_mix(int16_t *stereo, int frames);

#endif
