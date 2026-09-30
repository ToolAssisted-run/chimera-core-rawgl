/* SDL_mixer.h - the SDL_mixer calls rawgl's mixer.cpp makes, answered by the
 * core (sdl-shim.cpp): the music hook (rawgl's own mixer, the 3DO's songs),
 * the post-mix callback (the anniversary editions' and Windows 3.1's WAV
 * channels), the music (WAV, Ogg Vorbis, MIDI) and the channels (the 3DO's
 * AIFF sounds). The structures are the core's. */
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
