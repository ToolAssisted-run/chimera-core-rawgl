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
- **The gate**: 20 legs on the synthetic game, including the package through chimera-run.

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

- **The real game.** Nothing here has run Another World itself: no release's data is on this machine or on
  jaffanator2. `run-gate.sh -g <zip>` adds the equivalence, rerecord and session legs on it; the first run
  will say whether the protection screen, the parts' music sync and the password screen behave as on the
  synthetic game. Worth a look then: the step length a real part reports (the vsync declared, 25/2, is four
  fiftieths), the largest audio a step hands out, and how often a step is cut at a second.
- **Firmware declarations** (see above), once the releases' files can be hashed.
- **rawgl's licence**: ask its author for terms before the package is published anywhere.
- **Chimera's side**: the `AnotherWorld` system id and its mnemonics are Chimera's (SystemNames, MnemonicLookup),
  not this repository's.
- **CI**: `.github/workflows/chimera.yml` builds and gates as the other cores do; it publishes only when the
  repository is pushed, which it has not been.
