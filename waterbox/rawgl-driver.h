/* rawgl-driver.h - Another World (rawgl) as a machine that is stepped.
 *
 * The same driver is compiled for the miniBox guest and for the native
 * reference (run-native); wbx-entry.c puts the guest ABI on top of it.
 *
 * Wire format (waterbox.config "input.buttons", same order): the joystick
 * the game is played with (the arrows and the fire button, which the PC
 * keyboard's arrows and Space / Enter are), then the keyboard's commands -
 * Code (C, the password screen) and Pause (P) - then the letters and
 * Backspace, which only the password screen reads.
 */
#ifndef RAWGL_DRIVER_H
#define RAWGL_DRIVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum RawglButton
{
	RAWGL_BTN_UP,
	RAWGL_BTN_DOWN,
	RAWGL_BTN_LEFT,
	RAWGL_BTN_RIGHT,
	RAWGL_BTN_ACTION,      /* run / shoot: Space or Enter */
	RAWGL_BTN_CODE,        /* C: to the password screen */
	RAWGL_BTN_PAUSE,       /* P */
	RAWGL_BTN_LETTER_A,    /* A..Z: the password screen's letters */
	RAWGL_BTN_LETTER_Z = RAWGL_BTN_LETTER_A + 25,
	RAWGL_BTN_BACKSPACE,   /* the password screen's rub-out */
	RAWGL_BTN_COUNT
};

/* the original renderer's picture, the game's 320x200 pages */
#define RAWGL_VIDEO_WIDTH 320
#define RAWGL_VIDEO_HEIGHT 200

/* stereo, 44100 frames a second (the engine's mixing rate, as upstream's
 * SDL_mixer device); the game's sound is mono, the same on both sides */
#define RAWGL_AUDIO_RATE 44100
/* A step is cut at a second of the machine's time (a frame the game holds for
 * longer is split), which bounds the sound one step hands out */
#define RAWGL_MAX_STEP_MS 1000
#define RAWGL_AUDIO_MAX_SAMPLES 44200

/* 0 on failure, with the reason in err */
int rawgldrv_init(char *err, int errsize);
void rawgldrv_set_button(int index, int down);
/* runs the game to the end of its next step */
void rawgldrv_frame(int render);
const uint32_t *rawgldrv_video(void);
const int16_t *rawgldrv_audio(int *samples);   /* stereo frames, left then right */
int rawgldrv_input_was_read(void);
void rawgldrv_vsync(int *num, int *den);
uint64_t rawgldrv_clock(void);                 /* the machine's time, in milliseconds */

/* game-state: the property block, the raw domains, the table */
int rawgldrv_domain_count(void);
const char *rawgldrv_domain_name(int i);
uint8_t *rawgldrv_domain_ptr(int i);
int64_t rawgldrv_domain_size(int i);
int rawgldrv_domain_writable(int i);
const char *rawgldrv_game_properties(void);

#ifdef __cplusplus
}
#endif

#endif
