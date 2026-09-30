# chimera-core-rawgl

[rawgl](https://github.com/cyxx/rawgl), Gregory Montoir's reimplementation of the engine of Another World
(Out of This World; Delphine Software, Eric Chahi, 1991), as a [Chimera](https://github.com/ToolAssisted-run/chimera)
**game core** (`"kind": "game"`, see Chimera's `docs/game-cores.md`): the game's bytecode, its polygons, its
sound channels and music player, stepped one frame of the game at a time in miniBox's sandbox, packaged as
`rawgl.chimeraCore`.

**Built on upstream rawgl, with two patches**: the first serves the engine's file opens from memory (the
project's zip, unpacked at start), the second hands a fatal error to the core before rawgl would exit.
Everything else is rawgl compiled from source - its script interpreter, its software renderer, its mixer
(compiled as it is, against an SDL_mixer of the core's) - without its SDL and OpenGL frontend, which the core
is instead.

## What it is

- **Another World's DOS, Amiga and Atari ST releases** (and the Atari ST demo), from the user's own files: a
  project brings the game's folder as **one .zip** (the "game" slot) - the DOS release's `MEMLIST.BIN` and
  `BANK01`..`BANK0D`, or the Amiga's or the Atari ST's `BANK` files - at the zip's top or in a folder, names in
  any case. The core unpacks it into sealed memory at start (no savestate carries it). The package carries
  none of the game's data. The 15th and 20th Anniversary Editions, the Windows 3.1 and the 3DO releases are
  refused by name ("These are the 15th Anniversary Edition's files: the core plays the DOS, Amiga and Atari ST
  releases"); so are a project without the zip, a file that is not a zip, and a zip with no game in it.
- **The game starts where the original starts**: rawgl is built without its `BYPASS_PROTECTION` (upstream's
  Makefile defines it), so the DOS release with a password screen, the Amiga and the Atari ST begin at the
  copy protection's symbols, as the originals did. Nothing of the core offers a way around it.
- **A frame is one step of the game**: one frame the game shows, held for as many fiftieths of a second as its
  script says (`VAR_PAUSE_SLICES`; most of the game 4, 12.5 Hz). The pause is 50 ms steps until P comes again.
  `GetVsyncNumerator/Denominator` report the step just run (1000 / its milliseconds). Every step reads the
  controls. A step is cut at a second of the machine's time, which bounds its sound.
- **Time is the machine's**: rawgl's clock (`getTimeStamp`) is a counter only the game's own waits move, and a
  wait runs the engine's mixer for exactly the samples it covers - the music player tells the script where it
  is (`VAR_MUSIC_SYNC`), so the sound, and every scene that waits on it, is a function of the inputs. The one
  reading of the wall clock rawgl makes, at start, for its random seed, is the **Random seed** setting.
- **The controls are rawgl's**: P1 Up, Down, Left, Right (the joystick; Up jumps, Down crouches) and Action
  (fire: run, shoot, charge the gun; Space or Enter), then Code (C, to the password screen, where a release
  has one) and Pause (P), then the letters and Backspace, which only the password screen reads. A button held
  is the key held; a button pressed is the key typed, once. rawgl's screenshot and fast-mode keys are left out
  (the frontend's business; fast mode would change the timing), and so are its 3DO-only keys.
- **Settings**: Language (rawgl's `--language`: the texts, and the DOS release's title screen, "Another World"
  in French and "Out of This World" otherwise) and Random seed (0..65535).
- **Properties** (Chimera's `docs/game-cores.md`): a `Game State` block (the part, the part to come, the
  screen, the release, the language, the machine's steps and milliseconds, whether the music plays, whether
  the engine halted) and the game's **256 script variables** in place (`Script Variables`, writable): the
  engine's named ones (`VAR_HERO_*`, `VAR_MUSIC_SYNC`, `VAR_PAUSE_SLICES`...), Lester's and the world's under
  the names JaffarPlus's Another World gives them, and all 256 as `Var[0..255]`.
- **A fault in the data halts the machine, not the frontend**: rawgl's `error()` (an invalid opcode, a
  resource it cannot read) and its failed assertions stop the engine where it stands with the reason; the
  machine keeps stepping, silent, at 50 Hz, with `Machine.Halted` set.

## The patches

- `0001-memory-files.patch`: under `RAWGL_MEMFS`, rawgl's `File` opens through `rawgl_memfs_find()` - the
  core's table of the zip's files - and its case-insensitive directory walk is not built.
- `0002-error-hook.patch`: `error()` calls a weak `rawgl_error_hook()` before it exits; the core's halts the
  machine instead.

## Building

```
git submodule update --init
make -C waterbox -f native.mk -j$(nproc)    # the native reference and the harnesses
make -C waterbox -f guest.mk -j$(nproc)     # core.wbx
./waterbox/build-package.sh                 # build/package/rawgl.chimeraCore
```

miniBox is taken from `MB=`/`MINIBOX_DIR` (`-m` for the scripts), else `~/chimera/extern/chimera-common-minibox`,
with its C++ guest toolchain built (`meson setup build/meson-cpp -Dguest_cpp=true`). The patches go onto
`extern/rawgl` on the first build (`waterbox/apply-patches.sh`, all or nothing).

## The gate

`./waterbox/run-gate.sh [-m <miniBox>] [-c <chimera-run>] [-g <Another World zip>]`. Another World's data is
not the core's to carry, so the gate's content is **a game of its own**: `tests/make-synthetic.py` writes a
four-part "game" in the DOS release's format - its own bytecode, palettes, polygons, a sound and a music module
- which goes through the same path a real game takes: a copy-protection part to start at, an intro with music
that goes on by itself, a part the joystick plays (fire's sound, the seed's marker, the music's marker, the
pause, Code) and a password screen. Over it: native == sandbox (every step's picture, sound, length and lag,
the clock, the memory), a savestate before every step, a new host in the middle (while paused), turbo, the
two settings in both builds, the slot map, six refusals, the two halts in both builds, no host clock in the
guest, teeth, and with `-c` the package through Chimera's own engine (chimera-run). With `-g`, the
equivalence, rerecord and session legs also run on the real game from power-on.

## Where things are

- `waterbox/rawgl-driver.cpp`: the machine - rawgl's `SystemStub` answered by the core, the engine on its own
  stack (`coro.c`), the step, the clock, the input, the picture, the domains and the property table.
- `waterbox/sdl-shim.cpp`, `waterbox/compat/`: the SDL, SDL_mixer, zlib (miniz) and libmt32emu that rawgl's
  sources ask for; only the mixer's music hook does anything.
- `waterbox/files.c`: the zip, the slot map and the settings. `waterbox/halt.c`: a failed assertion halts.
- `waterbox/wbx-entry.c`: the exports. `run-native.c`, `run-wbx.c`, `gate-harness.h`: the harnesses.
- `waterbox/tests/`: the synthetic game and its movie.
- `docs/PLAN.md`: the decisions and what is left.

## Licence

This repository is GPL-2.0-or-later. **rawgl states no licence**: its repository carries no licence file and its
sources name Gregory Montoir's copyright without terms. Its predecessor, raw, is distributed by others under the
GPL (Fabien Sanglard's Another-World-Bytecode-Interpreter is GPL-2.0), but rawgl itself grants nothing in
writing; until its author states terms, a package of this core is for private use and not to be redistributed
(`waterbox/package-licenses.json`). The integration is GPL-2.0-or-later so that it stays compatible with
whichever GPL that turns out to be. miniz (`extern/miniz`) is MIT.
