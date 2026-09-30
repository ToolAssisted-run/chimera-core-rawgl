/* SDL.h - what rawgl's mixer.cpp asks of SDL, for a core that has no SDL.
 *
 * mixer.cpp is compiled as upstream wrote it; the audio device it opens is
 * the core's: nothing plays on its own, and the core calls the hook the mixer
 * registered for exactly the samples each step of the machine's time covers
 * (sdl-shim.cpp). There is no audio thread, so the locks are nothing. */
#ifndef RAWGL_CORE_SDL_H
#define RAWGL_CORE_SDL_H

#include <stdint.h>

typedef uint16_t SDL_AudioFormat;
#define AUDIO_S16SYS 0x8010

typedef struct SDL_RWops SDL_RWops;

static inline void SDL_LockAudio(void) {}
static inline void SDL_UnlockAudio(void) {}
SDL_RWops *SDL_RWFromConstMem(const void *mem, int size);

#endif
