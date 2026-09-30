/* SDL_mixer.h - the SDL_mixer calls rawgl's mixer.cpp makes, answered by the
 * core (sdl-shim.cpp). Only the music hook does anything: it is how the
 * engine's own mixer (its four sound channels and the music module player)
 * produces sound, and the core runs it at the end of every span of time the
 * machine sleeps. The mixer's chunk and music calls are the 3DO's, the
 * anniversary editions' and the MT-32's paths, which the core does not play:
 * they load nothing and play nothing. */
#ifndef RAWGL_CORE_SDL_MIXER_H
#define RAWGL_CORE_SDL_MIXER_H

#include "SDL.h"

typedef struct Mix_Chunk Mix_Chunk;
typedef struct Mix_Music Mix_Music;

#define MIX_MAX_VOLUME 128
#define MIX_INIT_OGG 0x10
#define MIX_INIT_MID 0x20

int Mix_Init(int flags);
void Mix_Quit(void);
int Mix_OpenAudio(int frequency, SDL_AudioFormat format, int channels, int chunksize);
void Mix_CloseAudio(void);
const char *Mix_GetError(void);
void Mix_HookMusic(void (*mix_func)(void *udata, uint8_t *stream, int len), void *arg);
void Mix_SetPostMix(void (*mix_func)(void *udata, uint8_t *stream, int len), void *arg);
int Mix_AllocateChannels(int numchans);
int Mix_Playing(int channel);
int Mix_PlayChannel(int channel, Mix_Chunk *chunk, int loops);
int Mix_HaltChannel(int channel);
void Mix_FreeChunk(Mix_Chunk *chunk);
int Mix_Volume(int channel, int volume);
Mix_Music *Mix_LoadMUS(const char *file);
int Mix_VolumeMusic(int volume);
int Mix_PlayMusic(Mix_Music *music, int loops);
int Mix_HaltMusic(void);
void Mix_FreeMusic(Mix_Music *music);
Mix_Chunk *Mix_LoadWAV_RW(SDL_RWops *src, int freesrc);

#endif
