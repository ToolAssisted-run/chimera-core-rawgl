/* mt32emu.h - the Munt calls rawgl's mixer.cpp makes for its MT-32 mixer
 * (the DOS release's --mt32), which the core does not offer: the mixer is
 * never set up that way, and these are never reached. */
#ifndef RAWGL_CORE_MT32EMU_H
#define RAWGL_CORE_MT32EMU_H

#include <stdint.h>

typedef struct mt32emu_data *mt32emu_context;
typedef struct { const void *v0; } mt32emu_report_handler_i;
enum { MT32EMU_MDM_IMMEDIATE = 0 };

static inline mt32emu_context mt32emu_create_context(mt32emu_report_handler_i r, void *d) { (void)r; (void)d; return 0; }
static inline int mt32emu_add_rom_file(mt32emu_context c, const char *f) { (void)c; (void)f; return -1; }
static inline void mt32emu_set_stereo_output_samplerate(mt32emu_context c, double r) { (void)c; (void)r; }
static inline int mt32emu_open_synth(mt32emu_context c) { (void)c; return -1; }
static inline void mt32emu_set_midi_delay_mode(mt32emu_context c, int m) { (void)c; (void)m; }
static inline void mt32emu_close_synth(mt32emu_context c) { (void)c; }
static inline void mt32emu_free_context(mt32emu_context c) { (void)c; }
static inline int mt32emu_play_msg(mt32emu_context c, uint32_t m) { (void)c; (void)m; return -1; }
static inline void mt32emu_render_bit16s(mt32emu_context c, int16_t *s, uint32_t n) { (void)c; (void)s; (void)n; }

#endif
