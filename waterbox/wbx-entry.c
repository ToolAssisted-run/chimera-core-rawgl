/* wbx-entry.c - the chimera guest ABI over rawgl-driver.
 *
 * Compiles identically for the guest (miniBox emulibc) and for the native
 * reference (native-shim/emulibc.h), which is what makes the equivalence gate
 * a real proof: the same driver, the same exports, one in the sandbox and one
 * out of it.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <emulibc.h>

#include "rawgl-driver.h"

static char g_load_error[1024];

/* A panel of more than 64 buttons arrives through SetButton only; the packed
 * mask covers the first 64. A step sees their union. */
static uint8_t g_set_buttons[RAWGL_BTN_COUNT];

/* Turbo: the picture is not converted. Only the conversion - the engine draws
 * its screen whatever happens, because what it draws is part of the game. */
ECL_INVISIBLE int chimera_render_enabled = 1;

ECL_EXPORT const char *GetLoadError(void) { return g_load_error; }

ECL_EXPORT int Init(void)
{
	g_load_error[0] = '\0';
	return rawgldrv_init(g_load_error, (int)sizeof g_load_error);
}

/* Jump is the 3DO's: the frontend asks after Init and hides it for the
 * other releases */
ECL_EXPORT int IsButtonActive(int32_t index) { return rawgldrv_button_active(index); }

ECL_EXPORT void SetButton(int32_t index, int32_t state)
{
	if (index >= 0 && index < RAWGL_BTN_COUNT) g_set_buttons[index] = state ? 1 : 0;
}

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	for (int i = 0; i < RAWGL_BTN_COUNT; i++)
		rawgldrv_set_button(i, g_set_buttons[i] | (i < 64 ? (int)((packed >> i) & 1) : 0));
	rawgldrv_frame(chimera_render_enabled);
}

ECL_EXPORT void SetRenderingEnabled(int on) { chimera_render_enabled = on != 0; }

/* the live size: the game's 320x200, or the 3DO's and Windows 3.1's
 * full-screen pictures at theirs (waterbox.config gives the capacity) */
ECL_EXPORT uint32_t *GetVideoBgra(void)
{
	int w, h;
	return (uint32_t *)rawgldrv_video(&w, &h);
}
ECL_EXPORT int GetVideoWidth(void)
{
	int w, h;
	rawgldrv_video(&w, &h);
	return w;
}
ECL_EXPORT int GetVideoHeight(void)
{
	int w, h;
	rawgldrv_video(&w, &h);
	return h;
}
/* 320x200 on a 4:3 monitor, as on the Amiga, the ST and the PC */
ECL_EXPORT int GetDisplayAspectX(void) { return 4; }
ECL_EXPORT int GetDisplayAspectY(void) { return 3; }

ECL_EXPORT int16_t *GetAudio(void)
{
	int n;
	return (int16_t *)rawgldrv_audio(&n);
}

ECL_EXPORT int GetAudioSampleCount(void)
{
	int n;
	rawgldrv_audio(&n);
	return n;
}

/* the length of the step just run, as a rate: 12.5 Hz for a frame of four
 * fiftieths of a second */
ECL_EXPORT int GetVsyncNumerator(void)
{
	int num, den;
	rawgldrv_vsync(&num, &den);
	return num;
}

ECL_EXPORT int GetVsyncDenominator(void)
{
	int num, den;
	rawgldrv_vsync(&num, &den);
	return den;
}

ECL_EXPORT int InputWasRead(void) { return rawgldrv_input_was_read(); }

/* memory domains: Game State (the property block), then the game's 256
 * script variables in place */
ECL_EXPORT int GetMemoryDomainCount(void) { return rawgldrv_domain_count(); }
ECL_EXPORT const char *GetMemoryDomainName(int i) { return rawgldrv_domain_name(i); }
ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i) { return rawgldrv_domain_ptr(i); }
ECL_EXPORT int64_t GetMemoryDomainSize(int i) { return rawgldrv_domain_size(i); }
ECL_EXPORT int GetMemoryDomainWritable(int i) { return rawgldrv_domain_writable(i); }

/* chimera docs/game-cores.md: the property table */
ECL_EXPORT const char *GetGameProperties(void) { return rawgldrv_game_properties(); }

/* the machine's own clock, in milliseconds, for harnesses that compare
 * machines */
ECL_EXPORT uint64_t GetCycleCount(void) { return rawgldrv_clock(); }
