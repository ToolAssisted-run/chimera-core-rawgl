/* sdl-shim.cpp - the audio device rawgl's mixer.cpp opens, as the core's.
 *
 * Upstream plays through SDL_mixer: the engine's mixer registers a callback
 * (Mix_HookMusic) that SDL's audio thread calls whenever the sound card wants
 * more, and that callback advances the music module player - whose patterns
 * also set a variable the game's script reads to keep in step with the music
 * (VAR_MUSIC_SYNC). On a real machine that happens as wall-clock time passes;
 * here it happens as the MACHINE's time passes: the driver calls
 * rawgl_audio_mix() for exactly the samples each sleep of the game covers,
 * on the game's own stack, between two instructions of its loop. So the sound
 * and everything the script learns from it are a function of the inputs.
 */
#include <string.h>

#include "compat/SDL_mixer.h"
#include "rawgl-audio.h"

typedef void (*mix_fn)(void *udata, uint8_t *stream, int len);

static mix_fn g_music;
static void *g_music_arg;
static mix_fn g_post;
static void *g_post_arg;

void rawgl_audio_mix(int16_t *stereo, int frames)
{
	const int len = frames * 2 * (int)sizeof(int16_t);
	memset(stereo, 0, (size_t)len);
	if (g_music) g_music(g_music_arg, (uint8_t *)stereo, len);
	if (g_post) g_post(g_post_arg, (uint8_t *)stereo, len);
}

int Mix_Init(int flags) { return flags; }
void Mix_Quit(void) {}
int Mix_OpenAudio(int frequency, SDL_AudioFormat format, int channels, int chunksize)
{
	(void)frequency; (void)format; (void)channels; (void)chunksize;
	return 0;
}
void Mix_CloseAudio(void)
{
	g_music = g_post = 0;
	g_music_arg = g_post_arg = 0;
}
const char *Mix_GetError(void) { return ""; }
void Mix_HookMusic(mix_fn f, void *arg) { g_music = f; g_music_arg = arg; }
void Mix_SetPostMix(mix_fn f, void *arg) { g_post = f; g_post_arg = arg; }
int Mix_AllocateChannels(int numchans) { return numchans; }
int Mix_Playing(int channel) { (void)channel; return 0; }
int Mix_PlayChannel(int channel, Mix_Chunk *chunk, int loops) { (void)channel; (void)chunk; (void)loops; return -1; }
int Mix_HaltChannel(int channel) { (void)channel; return 0; }
void Mix_FreeChunk(Mix_Chunk *chunk) { (void)chunk; }
int Mix_Volume(int channel, int volume) { (void)channel; (void)volume; return 0; }
Mix_Music *Mix_LoadMUS(const char *file) { (void)file; return 0; }
int Mix_VolumeMusic(int volume) { (void)volume; return 0; }
int Mix_PlayMusic(Mix_Music *music, int loops) { (void)music; (void)loops; return -1; }
int Mix_HaltMusic(void) { return 0; }
void Mix_FreeMusic(Mix_Music *music) { (void)music; }
SDL_RWops *SDL_RWFromConstMem(const void *mem, int size) { (void)mem; (void)size; return 0; }
Mix_Chunk *Mix_LoadWAV_RW(SDL_RWops *src, int freesrc) { (void)src; (void)freesrc; return 0; }
