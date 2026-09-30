# chimera-core-rawgl: plan and decisions

Started 2026-09-30, from the SDLPoP2 core's shape (the driver, the harnesses, the gate, the package).

## Done

- **M0, a native build**: rawgl's sources less `main.cpp`, `systemstub_sdl.cpp` and `graphics_gl.cpp`; the SDL
  and SDL_mixer it includes answered by `compat/` and `sdl-shim.cpp`; zlib by miniz (only the anniversary
  editions' reader uses it, and the core refuses those releases, but `resource_nth.cpp` is built as upstream
  builds it); libmt32emu by inert stubs (the MT-32 path needs ROMs and is not offered).
- **M1, virtual time**: `getTimeStamp()` is the machine's milliseconds, moved only by `sleep()`. A sleep runs
  the mixer's music hook for exactly its samples (`floor((t+ms)*44100/1000) - floor(t*44100/1000)`, so a split
  sleep gives the same count), on the game's own stack, so `VAR_MUSIC_SYNC` - which the music player sets and
  the script waits on - follows the inputs. `time()` is wrapped at link time (`-Wl,--wrap=time`): its one use in
  the game releases is the random seed, which is the setting. The guest has no `time` of its own; its only
  `clock_gettime` is musl's `__timedwait`, which the guest kit's pthread pulls in and nothing of the engine
  reaches.
- **M2, the guest build**: the musl/libstdc++ toolchain, the SDLPoP2 core's link recipe; check-wbx clean. rawgl
  needed nothing changed for it.
- **M3, the files**: patch 0001 (`File` from memory). The project's zip is found by the slot map (`game`), or
  mounted as `rom` by a host that is not a project's (chimera-run with a rom), or `game.zip` (the harnesses);
  the game's folder is the one holding the shallowest `MEMLIST.BIN`/`BANK01`/`AW.TOS`; names match without
  regard to case, as rawgl's own directory walk did.
- **M4-M5, picture, input, audio**: the software renderer's 555 frame to BGRA; the controls as levels, a
  press typed once where the step first reads the controls; 44100 Hz stereo from the engine's own mixer.
- **M6, savestates**: free - the engine's stack is guest memory (`coro.c`, MAP_STACK), the files are sealed.
  Rerecord (save and load before every step) and session (save while paused, new host, load) legs pass.
- **M7, the package**: `waterbox.config`, keybinds, `file_slots.json`, licences; a deterministic package.
- **The gate**: 46 legs on the synthetic game in every release's format, including the package through
  chimera-run.

## The other releases (2026-09-30)

Sergio: support every release rawgl plays, the real files to come later. Done, each proven on the synthetic game
in its own format (native == sandbox, rerecord, session), none yet on real data:

- **Files.** One slot takes a zip of any release's folder (found by what `Resource::detectVersion` looks for:
  MEMLIST.BIN, BANK01, AW.TOS, Data/Pak01.pak, game/DAT/FILE017.DAT, BANK, GameData/File340) or the 3DO's disc
  image as it is. The zip is streamed (miniz reads it from its file as it needs), so only what is unpacked takes
  memory. The disc is read in place by position (`rawgl_memfs_host_read`): miniBox's savestates carry the
  machine's memory and not the host's open files, so each read opens, reads and closes, and the 64 KiB cache is
  guest memory. The 20th's backgrounds other than 320x200 are not unpacked (the original renderer does not draw
  them). rawgl's `stat()` (the anniversary editions' folder checks, the 3DO's "is the data path a file") asks
  the core, through patch 0001.
- **rawgl's File reads past the end leave the byte uninitialised** (`File::readByte`'s `uint8_t b;`), and the
  3DO's song player reads 8 bytes past every song at every loop (it counts the SSND chunk's header as sound).
  The core's file layer gives zeros for what a read could not fill - the same as the padding a disc image has
  there, and the gate holds the 3DO's folder and disc to the same machine.
- **`rand()`**: the 20th picks among sound variants with it. glibc's and musl's generators differ, so the link
  wraps rand and srand with musl's (a 64-bit LCG) in both builds; its state is guest memory.
- **Sound.** The core's SDL_mixer (`sdl-shim.cpp`) plays the anniversary editions' and Windows 3.1's music
  (WAV; Ogg Vorbis with stb_vorbis; MIDI with TinySoundFont) and the 3DO's chunks (AIFF/AIFC PCM), mixed as
  SDL_mixer mixes (music, channels, post-mix), resampled linearly in integers. stb_vorbis and TinySoundFont use
  sin, cos, exp, log and pow, whose last bits differ between glibc and musl: `detmath.h` gives them the core's
  own (+ - * / and exact operations only), within a few ulps of glibc and identical in both builds.
- **Windows 3.1's MIDI** needs a SoundFont: an optional project slot ("soundfont", .sf2), loaded at Init with
  its samples (TinySoundFont's floats, twice the file) moved to sealed memory, so no savestate carries them.
  Without one the MIDI music is silent, as rawgl without a MIDI synthesizer; the gate checks that is the only
  difference (the MIDI music feeds nothing back to the script). A General MIDI SoundFont's hash could be
  declared as firmware instead, once one is chosen.
- **The input is events now** (all releases): a button pressed is a key going down, let go a key coming up, and
  nothing else touches rawgl's flags - so where the game consumes a press by clearing its flag (the 3DO's logos,
  title and menus; the pause), a held button is not pressed again until let go, as with SDL. Before this a held
  fire button went through all three 3DO logos and the title in one step.
- **The step rule**: a read of the controls ends the step only when the step has read them before and time has
  passed (the pause, the 3DO's logos: wait, read). A loop that waits before it reads and then shows its frame
  (the 3DO's title) is one step a frame.
- **The picture's live size**: the 3DO's and Windows 3.1's full-screen pictures (`drawBitmapOverlay`) at their
  own size, up to 640x480 (the 3DO's pause picture is 320x240); the game's pages 320x200.
- **The 3DO** runs in 15-bit colour (rawgl's software renderer with `_use555`, as rawgl picks for it), with its
  pad's Jump and its Back menu as buttons only it has (`IsButtonActive`).
- **Settings**: difficulty (the 20th's) and remasteredAudio (the anniversary editions'), which rawgl hands to the
  20th's script (variables 0xBF, 0xDE) and uses to pick sound files.

Decided for now, to revisit with the real data:

- **The anniversary editions in the original renderer** (320x200, their 320x200 pictures, the game's polygons):
  rawgl draws their HD pictures only with OpenGL. A software HD renderer would need 24-bit pages and redraw on
  palette changes (what GraphicsGL's draw lists do); OSMesa's softpipe in the core is the other way. Which, if
  either, once it is seen what the editions look like without.
- **Memory layout** `[64, 1024, 4, 4, 512]`: sealed sized for the anniversary editions and a SoundFont without
  their sizes known. The layout's size costs every savestate about 2 bytes a page of miniBox bookkeeping (the
  DOS synthetic's states went from 0.56 to 1.3 MB); size it to the largest real release, measured.

Found in rawgl, not changed:

- **The 3DO's Back menu, "yes"**: Engine::run sends the game to its title when the script asks for part 16000,
  but leaves `_res._nextPart` at 16000, so after the title restarts the game, setupTasks asks for 16000 again and
  Resource::setupPart stops with "invalid part" (the 3DO has no part 16000). Upstream's SDL build exits there;
  the core halts with that message. The fix is one line (clear `_nextPart` when going to the title) - a patch,
  if Sergio wants it, or a report upstream.
- **Windows 3.1's logos** (`kStateLogoWin31`) cannot be reached: Engine::setup moves Windows 3.1 to the copy
  protection before it checks for the intro, where the logos would play.

## Decisions

- **The step is a shown frame.** It ends in `updateScreen()`. Where time passes with nothing shown - the pause,
  whose loop reads the controls and sleeps 50 ms - it ends when the controls are next read. A step that reaches
  a second of the machine's time is cut there (the audio buffer's bound). A loop reading the controls 64 times
  with no time passing also ends one (a guard; no release is known to need it).
- **The input of a step is what the first read of the step sees.** rawgl reads the controls once a frame
  (`Script::updateInput`), before the tasks run; the pause and the 3DO's menus read in loops. One-shot keys
  (Code, Pause, the letters) are consumed by the first read.
- **The copy protection stays.** Upstream's Makefile defines `BYPASS_PROTECTION`; the core does not, so the game
  starts where the original does. `Game.Next Part` is read-only, so the property table offers no lever past it
  either (the script variables are writable, as memory is).
- **Game data as a zip slot, not firmware.** Chimera's convention for a game core is the original files as
  firmware, each declared with its hash. Another World has several releases whose files differ (DOS in more
  than one edition, Amiga in two languages, Atari ST), and this machine has none of them to hash, so the core
  takes the game's folder as one zip. When the data is at hand, declaring the releases' files as firmware (with
  `requiredWhen` on a release setting) is the move that brings it in line; the driver's file layer would read
  the mounted firmware by name instead of unpacking the zip.
- **Assertions stay on, and halt.** rawgl's checks are part of the program (upstream builds with them); the
  guest flags' `-DNDEBUG` is undone for rawgl (`-UNDEBUG`) so both builds keep them, and `halt.c` answers
  `__assert_fail`. miniz, which runs at Init off the engine's stack, has them off in both builds.
- **A halted machine steps at 50 Hz**, silent, its picture the last frame, so a frontend does not spin.
- **The EGA palette (`--ega-palette`) is not offered**: it changes only the DOS release's colours. It could be a
  setting (Chimera has no cosmetic settings: it would be part of the machine) if anyone wants it.
- **JaffarPlus** plays Another World with a `UDLRF` input string (its quickerRAWGL); a Chimera mnemonic table for
  the `AnotherWorld` system could mirror it. In Chimera's movie line the console buttons (Code, Pause, the
  letters, Backspace) come before P1's (Up, Down, Left, Right, Action), BizHawk's grouping.

## Left

- **The real game, every release.** Nothing here has run Another World itself: no release's data is on this
  machine or on jaffanator2 (Sergio will bring every release). `run-gate.sh -g <zip or iso>` adds the
  equivalence, rerecord and session legs on one. What the synthetic game cannot prove: the releases' scripts
  and copy protections, their compressed resources (DOS ByteKiller, Windows 3.1 LZ-Huffman, 3DO LZSS and coded
  cels, the 15th's TooDC), the Amiga and Atari ST's built-in resource tables (keyed on BANK01's size), the
  anniversary editions without their HD pictures, real step lengths and audio per step.
- **Releases as machines**: once each has run, expose them to Chimera to choose (as SDLPoP2's versions), which
  also lets firmware be required per release.
- **Firmware declarations** (see above), once the releases' files can be hashed.
- **rawgl's licence**: ask its author for terms before the package is published anywhere.
- **Chimera's side**: the `AnotherWorld` system id and its mnemonics are Chimera's (SystemNames, MnemonicLookup),
  not this repository's.
- **CI**: `.github/workflows/chimera.yml` builds and gates as the other cores do; it publishes only when the
  repository is pushed, which it has not been.
