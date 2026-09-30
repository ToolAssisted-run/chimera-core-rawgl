/* rawgl-driver.cpp - rawgl's engine as a machine that is stepped.
 *
 * rawgl runs Another World as the original program did: a loop that reads the
 * joystick, runs the game's 64 script tasks, and, when a task shows a frame,
 * sleeps what is left of the frame's length (VAR_PAUSE_SLICES fiftieths of a
 * second, sixtieths on the 3DO) before putting it on the screen; its pause,
 * and the 3DO's logos, title and menus, are loops that sleep until a key. The core keeps all of that and runs it on a stack of
 * its own (coro.c), with the platform (rawgl's SystemStub) answered here:
 *
 *   - Time is the machine's: getTimeStamp() is a counter that only a sleep
 *     moves, and a sleep runs the engine's mixer for exactly the samples it
 *     covers (sdl-shim.cpp) - the music player sets the variable the script
 *     keeps in step with the music (VAR_MUSIC_SYNC), so the sound belongs to
 *     the machine and not to the wall clock.
 *   - A step is one frame of the game: it ends when the frame is put on the
 *     screen (updateScreen). Where time passes without a frame - the pause,
 *     each 50 ms of it - the step ends when the game reads its controls again.
 *     A step that goes on for a second of the machine's time without either is
 *     cut there, which bounds its sound.
 *   - The controls arrive where the engine reads them (processEvents), as SDL
 *     events: a button pressed is a key going down, let go a key coming up
 *     (apply_input).
 *
 * The engine is upstream's, built from source with two patches (a file layer
 * served by the core, and a hook for a fatal error), its mixer compiled as it
 * is against the core's SDL_mixer (compat/, sdl-shim.cpp), its software
 * renderer for the picture, and none of its SDL or OpenGL frontend. Every
 * release rawgl plays is played here.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "engine.h"
#include "graphics.h"
#include "resource.h"
#include "script.h"
#include "systemstub.h"
#include "util.h"

#define MT32EMU_API_TYPE 1
#include <mt32emu.h>

#include "coro.h"
#include "midi.h"
#include "rawgl-audio.h"
#include "rawgl-driver.h"
#include "rawgl-files.h"

/* what upstream's main.cpp defines: the renderer's and the script's options */
bool Graphics::_is1991 = false;
bool Graphics::_use555 = false;
bool Video::_useEGA = false;
Difficulty Script::_difficulty = DIFFICULTY_NORMAL;
bool Script::_useRemasteredAudio = true;

namespace {

struct ChimeraStub : SystemStub
{
	void init(const char *title, const DisplayMode *dm) override { (void)title; (void)dm; }
	void fini() override {}
	void prepareScreen(int &w, int &h, float ar[4]) override;
	void updateScreen() override;
	void setScreenPixels555(const uint16_t *data, int w, int h) override;
	void processEvents() override;
	void sleep(uint32_t duration) override;
	uint32_t getTimeStamp() override;
};

struct Driver
{
	struct rawgl_settings settings;
	coro *co;
	Engine *engine;
	Graphics *graphics;
	ChimeraStub stub;
	int init_done, halted;
	char error[512];

	/* the buttons: held now, held on the step before, pressed and let go on
	 * this step (delivered at its first read of the controls) */
	uint8_t held[RAWGL_BTN_COUNT], prev[RAWGL_BTN_COUNT], pressed[RAWGL_BTN_COUNT], released[RAWGL_BTN_COUNT];

	/* the step */
	uint64_t clock_ms;      /* the machine's time */
	uint32_t step_ms;       /* of it, this step's */
	int polls, read, render;
	uint64_t steps;

	int16_t audio[RAWGL_AUDIO_MAX_SAMPLES * 2];
	int audio_frames;
	uint32_t video[RAWGL_VIDEO_MAX_WIDTH * RAWGL_VIDEO_MAX_HEIGHT];
	int video_w, video_h;
	Resource::DataType release;
};

Driver g;

/* ------------------------------------------------------------ the step */

void end_step()
{
	coro_yield(g.co);
	/* resumed by the next FrameAdvance: a new step (rawgldrv_frame reset it) */
}

/* samples for the machine's time from `from` for `ms`, exact over any split */
int samples_between(uint64_t from, uint32_t ms)
{
	return (int)((from + ms) * RAWGL_AUDIO_RATE / 1000 - from * RAWGL_AUDIO_RATE / 1000);
}

void advance(uint32_t ms)
{
	while (ms > 0)
	{
		if (g.step_ms >= RAWGL_MAX_STEP_MS)
		{
			end_step();
			continue;
		}
		uint32_t chunk = RAWGL_MAX_STEP_MS - g.step_ms;
		if (chunk > ms) chunk = ms;
		const int n = samples_between(g.clock_ms, chunk);
		if (g.audio_frames + n <= RAWGL_AUDIO_MAX_SAMPLES)
		{
			rawgl_audio_mix(g.audio + g.audio_frames * 2, n);
			g.audio_frames += n;
		}
		g.clock_ms += chunk;
		g.step_ms += chunk;
		ms -= chunk;
	}
}

/* The controls as rawgl's SDL frontend delivers them: events. A key going
 * down sets its flag, a key coming up clears it, and nothing else touches
 * it - so where the game consumes a press by clearing the flag itself (the
 * 3DO's logos, title and menus, the pause), a key still held is not pressed
 * again until it is let go and pressed anew, as upstream. A step's changes
 * arrive where it first reads the controls; a key pressed and released
 * between two steps does not exist, since a step sees only what is held. */
void apply_input(PlayerInput &pi)
{
	static const struct { int btn; uint8_t dir; } dirs[] = {
		{ RAWGL_BTN_UP, PlayerInput::DIR_UP }, { RAWGL_BTN_DOWN, PlayerInput::DIR_DOWN },
		{ RAWGL_BTN_LEFT, PlayerInput::DIR_LEFT }, { RAWGL_BTN_RIGHT, PlayerInput::DIR_RIGHT },
	};
	for (const auto &d : dirs)
	{
		if (g.pressed[d.btn]) pi.dirMask |= d.dir;
		if (g.released[d.btn]) pi.dirMask &= (uint8_t)~d.dir;
	}
	if (g.pressed[RAWGL_BTN_ACTION]) pi.action = true;
	if (g.released[RAWGL_BTN_ACTION]) pi.action = false;
	if (g.pressed[RAWGL_BTN_JUMP]) pi.jump = true;
	if (g.released[RAWGL_BTN_JUMP]) pi.jump = false;
	/* a key typed: once. rawgl's Back (the 3DO's end menu) is never pressed:
	 * the menu is not offered (docs/PLAN.md) */
	if (g.pressed[RAWGL_BTN_CODE]) pi.code = true;
	if (g.pressed[RAWGL_BTN_PAUSE]) pi.pause = true;
	for (int i = 0; i < 26; i++)
		if (g.pressed[RAWGL_BTN_LETTER_A + i]) pi.lastChar = (char)('a' + i);
	if (g.pressed[RAWGL_BTN_BACKSPACE]) pi.lastChar = 8;
	memset(g.pressed, 0, sizeof g.pressed);
	memset(g.released, 0, sizeof g.released);
}

/* ------------------------------------------------------------ the platform */

void ChimeraStub::prepareScreen(int &w, int &h, float ar[4])
{
	w = RAWGL_VIDEO_WIDTH;
	h = RAWGL_VIDEO_HEIGHT;
	ar[0] = ar[1] = 0.f;
	ar[2] = ar[3] = 1.f;
}

/* the game's page, or a full-screen picture of the 3DO's or Windows 3.1's
 * at its own size (cut to 640x480); the size is kept whether drawn or not */
void ChimeraStub::setScreenPixels555(const uint16_t *data, int w, int h)
{
	const int cw = w < RAWGL_VIDEO_MAX_WIDTH ? w : RAWGL_VIDEO_MAX_WIDTH;
	const int ch = h < RAWGL_VIDEO_MAX_HEIGHT ? h : RAWGL_VIDEO_MAX_HEIGHT;
	if (cw <= 0 || ch <= 0) return;
	g.video_w = cw;
	g.video_h = ch;
	if (!g.render) return;
	for (int y = 0; y < ch; y++)
		for (int x = 0; x < cw; x++)
		{
			const uint16_t c = data[y * w + x];
			const uint32_t r = (c >> 10) & 31, gr = (c >> 5) & 31, b = c & 31;
			g.video[y * cw + x] = 0xFF000000u | ((r << 3 | r >> 2) << 16) | ((gr << 3 | gr >> 2) << 8) | (b << 3 | b >> 2);
		}
}

void ChimeraStub::updateScreen() { end_step(); }

void ChimeraStub::processEvents()
{
	/* the controls read again after time passed with nothing shown (the
	 * pause, the 3DO's logos): that was a step, and what is read now belongs
	 * to the next; so does a loop that keeps reading without time passing,
	 * after 64 reads. A loop that waits BEFORE it reads and then shows its
	 * frame (the 3DO's title) is one step a frame: its first read is not a
	 * second one. */
	if (g.read && (g.step_ms > 0 || g.polls >= 64))
		end_step();
	g.read = 1;
	g.polls++;
	apply_input(_pi);
}

void ChimeraStub::sleep(uint32_t duration) { advance(duration); }

uint32_t ChimeraStub::getTimeStamp() { return (uint32_t)g.clock_ms; }

/* ------------------------------------------------------------ the program */

const char *release_name(Resource::DataType t)
{
	switch (t)
	{
	case Resource::DT_DOS: return "DOS";
	case Resource::DT_AMIGA: return "Amiga";
	case Resource::DT_ATARI: return "Atari ST";
	case Resource::DT_ATARI_DEMO: return "Atari ST demo";
	case Resource::DT_15TH_EDITION: return "15th Anniversary Edition";
	case Resource::DT_20TH_EDITION: return "20th Anniversary Edition";
	case Resource::DT_WIN31: return "Windows 3.1";
	case Resource::DT_3DO: return "3DO";
	}
	return "unknown";
}

void halt(const char *msg)
{
	snprintf(g.error, sizeof g.error, "%s", msg);
	g.halted = 1;
	for (;;) coro_yield(g.co);
}

void game_main()
{
	/* rawgl's main(), as the core runs it: the data path is the project's
	 * folder, or its 3DO disc (the files are the zip's, patches/0001), the
	 * game from its start - which on the DOS, Amiga, Atari ST and Windows 3.1
	 * releases is the copy protection's symbols, as upstream builds it without
	 * BYPASS_PROTECTION - and rawgl's options from the settings (the 20th
	 * Anniversary Edition's difficulty, the anniversary editions' sound) */
	Script::_difficulty = (Difficulty)g.settings.difficulty;
	Script::_useRemasteredAudio = g.settings.remastered_audio != 0;
	g.engine = new Engine(rawgl_files_data_dir(), kPartIntro);
	const Resource::DataType type = g.engine->_res.getDataType();
	g.release = type;
	/* the renderer: rawgl's software one, as its "original" renderer for the
	 * 1991 releases and the anniversary editions (which draws their 320x200
	 * pictures and the game's polygons; their HD pictures are its OpenGL
	 * renderer's), and in 15-bit colour for the 3DO, as rawgl picks for it */
	Graphics::_use555 = (type == Resource::DT_3DO);
	Graphics::_is1991 = (type != Resource::DT_3DO);
	/* Windows 3.1's MIDI music: the project's SoundFont, loaded now (at Init,
	 * so its samples can be sealed) */
	if (type == Resource::DT_WIN31)
	{
		FILE *sf2 = rawgl_files_soundfont();
		if (sf2)
		{
			const int ok = midi_load_soundfont(sf2);
			fclose(sf2);
			if (!ok) halt("the project's SoundFont could not be read (a .sf2 file)");
		}
	}
	/* the DOS release's sound effects on a Roland CM-32L (rawgl's --mt32):
	 * Munt, with the ROMs rawgl opens by name, which the project brings as
	 * firmware - refused here, with their names, rather than silent */
	const bool mt32 = g.settings.mt32 && type == Resource::DT_DOS;
	if (mt32)
	{
		/* each ROM tried in a context of its own, as rawgl's mixer will add it */
		static const char *const roms[] = { "CM32L_CONTROL.ROM", "CM32L_PCM.ROM" };
		for (const char *rom : roms)
		{
			char msg[200];
			FILE *f = fopen(rom, "rb");
			if (!f)
			{
				snprintf(msg, sizeof msg, "the MT-32 sound effects need the CM-32L's ROMs: %s is not there", rom);
				halt(msg);
			}
			fclose(f);
			mt32emu_report_handler_i none = { 0 };
			mt32emu_context c = mt32emu_create_context(none, 0);
			const mt32emu_return_code rc = mt32emu_add_rom_file(c, rom);
			mt32emu_free_context(c);
			if (rc != MT32EMU_RC_ADDED_CONTROL_ROM && rc != MT32EMU_RC_ADDED_PCM_ROM)
			{
				snprintf(msg, sizeof msg, "%s is not a Roland ROM Munt knows (a CM-32L's, or an MT-32's)", rom);
				halt(msg);
			}
		}
	}
	g.graphics = GraphicsSoft_create();
	g.engine->setSystemStub(&g.stub, g.graphics);
	g.engine->setup((Language)g.settings.language, GRAPHICS_ORIGINAL, "", 1, mt32);
	g.init_done = 1;
	coro_yield(g.co);
	for (;;) g.engine->run();
}

} // namespace

/* patches/0002: a fatal error in the engine halts the machine, with its
 * message (and so does a failed assertion, halt.c) */
extern "C" void rawgl_error_hook(const char *msg) { halt(msg); }

/* the engine's clock reading at start (the script's random seed, and the 20th
 * Anniversary Edition's srand) is the project's setting (the link wraps
 * time()) */
extern "C" time_t __wrap_time(time_t *t)
{
	const time_t v = (time_t)g.settings.random_seed;
	if (t) *t = v;
	return v;
}

/* the 20th Anniversary Edition picks among its sound variants with rand():
 * musl's generator (the link wraps rand and srand), so the native reference
 * - glibc's rand is another sequence - draws the same numbers as the sandbox,
 * and its state is guest memory, which a savestate carries */
static uint64_t g_rand_seed;
extern "C" void __wrap_srand(unsigned s) { g_rand_seed = s - 1; }
extern "C" int __wrap_rand(void)
{
	g_rand_seed = 6364136223846793005ULL * g_rand_seed + 1;
	return (int)(g_rand_seed >> 33);
}

/* ------------------------------------------------------------ the exports */

extern "C" {

int rawgldrv_init(char *err, int errsize)
{
	memset(&g.held, 0, sizeof g.held);
	for (int i = 0; i < RAWGL_VIDEO_MAX_WIDTH * RAWGL_VIDEO_MAX_HEIGHT; i++) g.video[i] = 0xFF000000u;
	g.video_w = RAWGL_VIDEO_WIDTH;
	g.video_h = RAWGL_VIDEO_HEIGHT;
	rawgl_settings_read(&g.settings);
	if (g.settings.random_seed < 0 || g.settings.random_seed > 65535)
	{
		snprintf(err, (size_t)errsize, "the setting randomSeed is %ld; it goes from 0 to 65535", g.settings.random_seed);
		return 0;
	}
	if (!rawgl_files_load(err, errsize)) return 0;
	g.co = coro_create(game_main, 1 << 20);
	if (!g.co)
	{
		snprintf(err, (size_t)errsize, "no memory for the game's stack");
		return 0;
	}
	g.render = 1;
	coro_resume(g.co);
	if (g.halted || !g.init_done)
	{
		snprintf(err, (size_t)errsize, "%s", g.halted ? g.error : "the game did not start");
		return 0;
	}
	return 1;
}

void rawgldrv_set_button(int index, int down)
{
	if (index >= 0 && index < RAWGL_BTN_COUNT) g.held[index] = down ? 1 : 0;
}

void gamestate_from_game(void);

void rawgldrv_frame(int render)
{
	for (int i = 0; i < RAWGL_BTN_COUNT; i++)
	{
		g.pressed[i] = g.held[i] && !g.prev[i];
		g.released[i] = !g.held[i] && g.prev[i];
		g.prev[i] = g.held[i];
	}
	g.step_ms = 0;
	g.polls = 0;
	g.read = 0;
	g.audio_frames = 0;
	g.render = render;
	if (!g.halted)
	{
		coro_resume(g.co);
		g.steps++;
	}
	gamestate_from_game();
}

const uint32_t *rawgldrv_video(int *w, int *h)
{
	*w = g.video_w;
	*h = g.video_h;
	return g.video;
}

int rawgldrv_button_active(int index)
{
	if (index == RAWGL_BTN_JUMP) return g.release == Resource::DT_3DO;
	return index >= 0 && index < RAWGL_BTN_COUNT;
}

const int16_t *rawgldrv_audio(int *samples)
{
	*samples = g.audio_frames;
	return g.audio;
}

int rawgldrv_input_was_read(void) { return g.read; }

/* the step's length as a rate: 1000 / its milliseconds (a frame of 4 slices,
 * 80 ms, is 12.5 Hz); a step that took no time at all is reported as 1 ms.
 * A halted machine's steps are nothing, shown at 50 Hz. */
void rawgldrv_vsync(int *num, int *den)
{
	*num = 1000;
	*den = g.step_ms ? (int)g.step_ms : g.halted ? 20 : 1;
}

uint64_t rawgldrv_clock(void) { return g.clock_ms; }

/* ------------------------------------------------------------ game state
 *
 * chimera docs/game-cores.md: the properties are a labelled memory domain.
 * "Game State" is the core's own block, copied from the engine after every
 * step, all of it read-only (a part to jump to is not offered: the game
 * starts where the original starts, at its copy protection); "Script
 * Variables" is the engine's own array of the game's 256 variables, in place
 * and writable - the game keeps everything it knows about Lester, the aliens
 * and the level there. */

struct __attribute__((packed)) GameState
{
	uint16_t part;          /* 0 */
	uint16_t next_part;     /* 2: the part the game goes to at its next frame (0: none) */
	int16_t screen;         /* 4 */
	uint8_t release;        /* 6 */
	uint8_t language;       /* 7 */
	uint64_t steps;         /* 8 */
	uint64_t time_ms;       /* 16 */
	uint8_t music_playing;  /* 24 */
	uint8_t halted;         /* 25 */
	uint8_t pad[6];
};

static GameState gs;

void gamestate_from_game(void)
{
	if (!g.engine) return;
	gs.part = g.engine->_res._currentPart;
	gs.next_part = g.engine->_res._nextPart;
	gs.screen = (int16_t)g.engine->_script._screenNum;
	gs.release = (uint8_t)g.engine->_res.getDataType();
	gs.language = (uint8_t)g.settings.language;
	gs.steps = g.steps;
	gs.time_ms = g.clock_ms;
	gs.music_playing = g.engine->_ply._playing ? 1 : 0;
	gs.halted = (uint8_t)g.halted;
}

int rawgldrv_domain_count(void) { return 2; }

const char *rawgldrv_domain_name(int i) { return i == 0 ? "Game State" : i == 1 ? "Script Variables" : ""; }

uint8_t *rawgldrv_domain_ptr(int i)
{
	if (i == 0) return (uint8_t *)&gs;
	if (i == 1 && g.engine) return (uint8_t *)g.engine->_script._scriptVars;
	return NULL;
}

int64_t rawgldrv_domain_size(int i) { return i == 0 ? (int64_t)sizeof gs : i == 1 ? 512 : 0; }

int rawgldrv_domain_writable(int i) { return i == 1; }

/* the game's variables that have a name: the engine's own (script.h VAR_*)
 * and those Another World's script keeps Lester and the world in (the names
 * JaffarPlus's Another World gives them, games/raw/anotherWorld) */
static const struct { int var; const char *name, *group, *desc; } k_vars[] = {
	{ 0x01, "Lester.X", "Lester", "Horizontal position in the room" },
	{ 0x02, "Lester.Y", "Lester", "Vertical position in the room" },
	{ 0x03, "Lester.Dead State", "Lester", "Nonzero while Lester dies" },
	{ 0x06, "Lester.Gun Ammo", "Lester", "The gun's charge" },
	{ 0x0A, "Lester.Has Gun", "Lester", "Nonzero once Lester has the gun" },
	{ 0x0F, "Lester.Gun Load", "Lester", "The shot being charged (also the animation state)" },
	{ 0x14, "World.Elevator Y", "World", "An elevator's vertical position" },
	{ 0x15, "Lester.Momentum 1", "Lester", "" },
	{ 0x16, "Lester.Momentum 2", "Lester", "" },
	{ 0x17, "Lester.Momentum 3", "Lester", "" },
	{ 0x2A, "World.Script State", "World", "" },
	{ 0x2B, "World.Script State 2", "World", "" },
	{ 0x31, "World.Timer", "World", "A countdown the level's script runs" },
	{ 0x3C, "Engine.Random Seed", "Engine", "The script's random number generator (the randomSeed setting starts it)" },
	{ 0x63, "Lester.Direction", "Lester", "Also Lester's state" },
	{ 0x66, "Lester.Room", "Lester", "The room Lester is in" },
	{ 0x67, "Engine.Screen", "Engine", "The screen shown (VAR_SCREEN_NUM)" },
	{ 0x68, "Alien.X", "Alien", "The alien's horizontal position" },
	{ 0x6A, "Alien.Room", "Alien", "The room the alien is in" },
	{ 0x6B, "Alien.State", "Alien", "" },
	{ 0xDA, "Engine.Last Key", "Engine", "The letter typed on the password screen (VAR_LAST_KEYCHAR)" },
	{ 0xE5, "Engine.Hero Up Down", "Engine", "The joystick's up / down, as the game reads it (VAR_HERO_POS_UP_DOWN; Lester's swimming)" },
	{ 0xE8, "World.Fumes State", "World", "" },
	{ 0xF4, "Engine.Music Sync", "Engine", "What the music last told the script (VAR_MUSIC_SYNC)" },
	{ 0xF9, "Engine.Scroll Y", "Engine", "The screen's vertical scroll (VAR_SCROLL_Y)" },
	{ 0xFA, "Engine.Hero Action", "Engine", "The fire button, as the game reads it (VAR_HERO_ACTION)" },
	{ 0xFB, "Engine.Hero Jump Down", "Engine", "VAR_HERO_POS_JUMP_DOWN" },
	{ 0xFC, "Engine.Hero Left Right", "Engine", "VAR_HERO_POS_LEFT_RIGHT" },
	{ 0xFD, "Engine.Hero Pos Mask", "Engine", "VAR_HERO_POS_MASK" },
	{ 0xFE, "Engine.Hero Action Pos Mask", "Engine", "VAR_HERO_ACTION_POS_MASK" },
	{ 0xFF, "Engine.Pause Slices", "Engine", "The frame's length in fiftieths of a second (VAR_PAUSE_SLICES)" },
};

const char *rawgldrv_game_properties(void)
{
	static char json[16384];
	if (json[0]) return json;
	int n = 0;
#define P(...) n += snprintf(json + n, sizeof json - (size_t)n, __VA_ARGS__)
	P("{\n  \"properties\": [\n");
	P("    { \"name\": \"Game.Part\", \"domain\": \"Game State\", \"offset\": 0, \"type\": \"u16\", \"group\": \"Game\", \"writable\": false, "
	  "\"values\": { \"16000\": \"Copy Protection\", \"16001\": \"Intro\", \"16002\": \"Water\", \"16003\": \"Prison\", \"16004\": \"Cite\", "
	  "\"16005\": \"Arena\", \"16006\": \"Luxe\", \"16007\": \"Final\", \"16008\": \"Password\" }, \"description\": \"The part of the game being played\" },\n");
	P("    { \"name\": \"Game.Next Part\", \"domain\": \"Game State\", \"offset\": 2, \"type\": \"u16\", \"group\": \"Game\", \"writable\": false, "
	  "\"description\": \"The part the game goes to at its next frame (0: none)\" },\n");
	P("    { \"name\": \"Game.Screen\", \"domain\": \"Game State\", \"offset\": 4, \"type\": \"s16\", \"group\": \"Game\", \"writable\": false, "
	  "\"description\": \"The screen the script last loaded\" },\n");
	P("    { \"name\": \"Game.Release\", \"domain\": \"Game State\", \"offset\": 6, \"type\": \"u8\", \"group\": \"Game\", \"writable\": false, \"values\": {");
	for (int t = Resource::DT_DOS; t <= Resource::DT_ATARI_DEMO; t++)
		P("%s \"%d\": \"%s\"", t ? "," : "", t, release_name((Resource::DataType)t));
	P(" } },\n");
	P("    { \"name\": \"Game.Language\", \"domain\": \"Game State\", \"offset\": 7, \"type\": \"u8\", \"group\": \"Game\", \"writable\": false, "
	  "\"values\": { \"0\": \"French\", \"1\": \"English\", \"2\": \"German\", \"3\": \"Spanish\", \"4\": \"Italian\" } },\n");
	P("    { \"name\": \"Machine.Steps\", \"domain\": \"Game State\", \"offset\": 8, \"type\": \"u64\", \"group\": \"Machine\", \"writable\": false },\n");
	P("    { \"name\": \"Machine.Time Ms\", \"domain\": \"Game State\", \"offset\": 16, \"type\": \"u64\", \"group\": \"Machine\", \"writable\": false, "
	  "\"description\": \"The machine's time since it started, in milliseconds\" },\n");
	P("    { \"name\": \"Machine.Music Playing\", \"domain\": \"Game State\", \"offset\": 24, \"type\": \"bool\", \"group\": \"Machine\", \"writable\": false },\n");
	P("    { \"name\": \"Machine.Halted\", \"domain\": \"Game State\", \"offset\": 25, \"type\": \"bool\", \"group\": \"Machine\", \"writable\": false, "
	  "\"description\": \"The engine stopped on an error\" },\n");
	for (size_t i = 0; i < sizeof k_vars / sizeof k_vars[0]; i++)
	{
		P("    { \"name\": \"%s\", \"domain\": \"Script Variables\", \"offset\": %d, \"type\": \"s16\", \"group\": \"%s\"", k_vars[i].name, k_vars[i].var * 2, k_vars[i].group);
		if (k_vars[i].desc[0]) P(", \"description\": \"%s (variable 0x%02X)\"", k_vars[i].desc, k_vars[i].var);
		else P(", \"description\": \"Variable 0x%02X\"", k_vars[i].var);
		P(" },\n");
	}
	P("    { \"name\": \"Var\", \"domain\": \"Script Variables\", \"offset\": 0, \"type\": \"s16\", \"count\": 256, \"stride\": 2, \"group\": \"Script Variables\", "
	  "\"description\": \"The game's 256 variables, by number\" }\n");
	P("  ]\n}\n");
#undef P
	return json;
}

} // extern "C"
