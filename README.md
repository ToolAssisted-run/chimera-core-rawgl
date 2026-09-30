# chimera-core-rawgl

[rawgl](https://github.com/cyxx/rawgl), Gregory Montoir's reimplementation of the engine of Another World
(Out of This World; Delphine Software, Eric Chahi, 1991), as a [Chimera](https://github.com/ToolAssisted-run/chimera)
**game core** (`"kind": "game"`, see Chimera's `docs/game-cores.md`): the game's bytecode, its polygons, its
sound channels and music player, stepped one frame of the game at a time in miniBox's sandbox, packaged as
`rawgl.chimeraCore`.

**Built on upstream rawgl, with two patches**: the first serves the engine's file opens (and its `stat()`)
from the project's file, the second hands a fatal error to the core before rawgl would exit. Everything else is
rawgl compiled from source - its script interpreter, its software renderer, its mixer (compiled as it is,
against an SDL_mixer of the core's) - without its SDL and OpenGL frontend, which the core is instead. The
core's SDL_mixer plays what rawgl asks of it with miniz, zlib, stb_vorbis and TinySoundFont, over its own
deterministic math (`waterbox/detmath.h`), so the sandbox and the native reference make the same sound, and
the CM-32L is Munt's.

## What it is

- **Every release rawgl plays**, from the user's own files: a project brings the game as **one file** (the
  "game" slot) - a .zip of the game's folder, at its top or in a folder, names in any case, or the 3DO disc as it
  is: its image (.iso) or MAME's compressed image of it (.chd, read with libchdr: its first data track is what
  rawgl reads as the disc). The core unpacks a zip into sealed memory at start (no savestate carries it) and
  reads a disc where it lies, a block at a time, holding nothing of the host's between two reads. The package
  carries none of the game's data. A project without the file, a file that is not a zip or a disc, a zip with no
  game in it, and data rawgl cannot tell are refused, saying why.

  | Release | The folder is recognised by | Starts at | Sound | Picture |
  |---|---|---|---|---|
  | DOS | MEMLIST.BIN + BANK01..BANK0D | the copy protection (the demo: the intro) | rawgl's 4 channels and module player; the effects on a CM-32L with the mt32 setting | original, 320x200 |
  | DOS demo | MEMLIST.BIN + DEMO01.. | the intro | the same | the same |
  | Amiga (French, English) | BANK01 of 244,674 / 244,868 bytes | the copy protection | the same | the same |
  | Atari ST | BANK01 of 227,142 bytes | the copy protection | the same | the same |
  | Atari ST demo | AW.TOS of 96,513 bytes | the intro | the same | the same |
  | 15th Anniversary Edition | Data/Pak01.pak | the intro | WAV sounds, WAV music (original or remastered) | original, 320x200 (see below) |
  | 20th Anniversary Edition | game/DAT/FILE017.DAT | the intro | gzip'd WAV sounds, Ogg Vorbis music (original or remastered) | original, 320x200 (see below) |
  | Windows 3.1 | BANK (+ WORLD.EXE) | the copy protection | WAV sounds, MIDI music (with the project's SoundFont) | original, 320x200 |
  | 3DO | GameData/File340, or the disc (.iso, .chd) | its logos and title | AIFF sounds, SDX2 AIFF-C songs | 15-bit colour, and its full-screen pictures |

  The anniversary editions run in rawgl's software renderer, as its "original" renderer draws them: the game's
  polygons and its 320x200 pictures, at 320x200. Their HD pictures (1280x800 and up) are drawn only by rawgl's
  OpenGL renderer, which the core does not have; whether a software renderer for them is worth writing is a
  question for when the editions' data is at hand.
- **The game starts where the original starts**: rawgl is built without its `BYPASS_PROTECTION` (upstream's
  Makefile defines it), so the DOS release with a password screen, the Amiga, the Atari ST and Windows 3.1
  begin at the copy protection's symbols, as the originals did. Nothing of the core offers a way around it.
- **A frame is one step of the game**: one frame the game shows, held for as many fiftieths of a second as its
  script says (`VAR_PAUSE_SLICES`; most of the game 4, 12.5 Hz), sixtieths on the 3DO. The pause, and the
  3DO's logos and title, are 50 ms steps while the game waits. `GetVsyncNumerator/Denominator` report
  the step just run (1000 / its milliseconds). A step is cut at a second of the machine's time.
- **Time is the machine's**: rawgl's clock (`getTimeStamp`) is a counter only the game's own waits move, and a
  wait runs the engine's mixer for exactly the samples it covers - the music player tells the script where it
  is (`VAR_MUSIC_SYNC`), so the sound, and every scene that waits on it, is a function of the inputs. rawgl's
  readings of the wall clock (the random seed, the 20th's `srand`) are the **Random seed** setting, and its
  `rand()` is one generator in every build.
- **The controls are rawgl's**: P1 Up, Down, Left, Right (Up jumps, Down crouches), Action (fire: Space or
  Enter) and, on the 3DO, Jump (Shift); then Code (C) and Pause (P); then the letters and Backspace, which
  only the password screen reads. A button pressed is a key going down and a button let go a key coming up, as
  rawgl's SDL frontend sees them - so where the game takes a press (the title, a picture it waits on), a button
  still held is not pressed again until it is let go. Jump exists on the 3DO only (`IsButtonActive`). The 3DO's
  Back key, which opens its end menu, is not offered: its "yes" stops rawgl (docs/PLAN.md).
- **Settings**: Language, Random seed (0..65535), Difficulty (the 20th's, rawgl's `--difficulty`),
  Remastered Sound (the anniversary editions', rawgl's `--audio`) and MT-32 Sound Effects (the DOS release's,
  rawgl's `--mt32`).
- **The MT-32**: with the mt32 setting, rawgl sends the DOS release's sound effects it has a note for (41 of
  them) to a Roland CM-32L's rhythm part instead of playing the game's samples - Munt's libmt32emu, compiled in,
  with the CM-32L's ROMs the project brings as firmware (`CM32L_CONTROL.ROM`, `CM32L_PCM.ROM`; another pair Munt
  knows may take their place). The music stays the game's own. A project with the setting and without the ROMs,
  or with files that are not ROMs, is refused, naming them.
- **Properties** (Chimera's `docs/game-cores.md`): a `Game State` block (the part, the part to come, the
  screen, the release, the language, the machine's steps and milliseconds, whether the music plays, whether
  the engine halted) and the game's **256 script variables** in place (`Script Variables`, writable): the
  engine's named ones (`VAR_HERO_*`, `VAR_MUSIC_SYNC`, `VAR_PAUSE_SLICES`...), Lester's and the world's under
  the names JaffarPlus's Another World gives them, and all 256 as `Var[0..255]`.
- **A fault in the data halts the machine, not the frontend**: rawgl's `error()` and its failed assertions stop
  the engine where it stands with the reason; the machine keeps stepping, silent, at 50 Hz, with
  `Machine.Halted` set.

## What has been run

**The DOS, Amiga (English) and 3DO releases have run on real data** (below). The other releases' real data
has not been at hand yet. Every
release has also run as the core's **synthetic game** - its own bytecode, pictures, sounds and music written in that release's
format (`waterbox/tests/make-synthetic.py`) - through the whole path its files take: native == sandbox,
rerecord, session. That proves the core's side (the files, the step, the clock, the sound decoders, the
savestates) on each release's formats; it does not prove the releases' own content (their scripts, their
compressed resources - the DOS banks' ByteKiller packing, Windows 3.1's LZ-Huffman, the 3DO's LZSS and coded
cels as the real files use them, the 15th's TooDC encoding - their copy protections, their timing). `run-gate.sh
-g <zip or iso> [-M <movie>]` runs the equivalence, rerecord and session legs on a real release.

**Out of This World (US DOS, a verified copy: GoodOldDays 000568, one 1.2 MB disk)**: the copy protection's
screen asks for code-wheel symbols as the original does, and takes the right ones (entered with the joystick as
an owner reads them off the wheel; the test movie stays out of the repo); its credits, then the 152-second intro
- whose script waits on the music, so its timing is the music player's by the machine's time - then the first
level: Lester swims out of the pool onto the first screen, walks on, and a leech gets him (the game's own
close-up, then its "press button" screen with the level's access code). Over those 4,040 steps (3 min 47 s):
native == sandbox, a savestate before every step, a new host on the first level; with the mt32 setting and real
CM-32L ROMs, native == sandbox and only the sound changing, where the intro's effects play.

**Another World (Amiga, English: two OFS floppies, archive.org's "anotherworldamiga")**: a project brings the
two disks' files merged into one folder, as the disks' own readme installs them (bank06, 09 and 0C are on both,
the same; the Amiga has no bank05). rawgl recognises it by BANK01's size (244,868 bytes) and finds its
resources by its own table. The same path as DOS - the protection (whose picker is slower here: its cursor
takes up to 25 frames to follow a press), the credits, the 148-second intro (the Amiga's music runs a little
shorter than the DOS port's), the first level to the leech - 3,767 steps: native == sandbox, rerecord, session.

**Out of This World (3DO, USA: a CHD, MAME's compressed image - one MODE1_RAW track of 164,290 frames)**: the
project brings the .chd as it is; libchdr reads it (LZMA, zlib and FLAC hunks), a hunk at a time, through
callbacks that open, read and close the mounted file, so a savestate holds nothing of the host's. The Interplay
logo and the others with the 3DO's songs, the title, the 160-second intro at the 3DO's 60 Hz timings with its
painted backgrounds, the first level to the leech and the 3DO's own continue screen - 3,907 steps: native ==
sandbox, rerecord, session; and chimera-run taking the .chd as its rom.

| Release | Synthetic game | Real files |
|---|---|---|
| DOS | yes, and its effects on a CM-32L (with real CM-32L ROMs) | **yes**: Out of This World (US), the copy protection answered, its credits, the whole intro, the first level; native == sandbox, rerecord, session; and with the CM-32L |
| DOS demo | (the DOS path, less the password screen) | needs files |
| Amiga English | no (rawgl finds its resources by a built-in table keyed on BANK01's size) | **yes**: the two disks' files, the copy protection answered, the whole intro, the first level; native == sandbox, rerecord, session |
| Amiga French | no (the same) | needs files |
| Atari ST | no (the same) | needs files |
| Atari ST demo | no (the same, AW.TOS) | needs files |
| 15th Anniversary Edition | yes: Pak01.pak, WAV sounds and music, original and remastered | needs files |
| 20th Anniversary Edition | yes: game/, gzip'd sounds, Ogg music, difficulty, original and remastered | needs files |
| Windows 3.1 | yes: BANK (unpacked entries), its palettes, WAV, MIDI with a SoundFont | needs files (and a SoundFont) |
| 3DO | yes: GameData/, a disc image and a CHD, logos and title, AIFF, SDX2, a coded cel | **yes**: Out of This World (USA), a CHD; the logos, the title, the whole intro, the first level; native == sandbox, rerecord, session; and through chimera-run |

## The patches

- `0001-memory-files.patch`: under `RAWGL_MEMFS`, rawgl's `File` opens through the core - `rawgl_memfs_find()`,
  the zip's files in memory, or `rawgl_memfs_host_read()`, a disc image read by position - its `stat()` asks
  the core too, a read past a file's end gives zeros (rawgl's `File::readByte` leaves its byte uninitialised
  otherwise, and the 3DO's song player reads past each song), and its case-insensitive directory walk is not
  built.
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

`./waterbox/run-gate.sh [-m <miniBox>] [-c <chimera-run>] [-g <Another World zip or iso>] [-r <ROM dir>]`. Another World's data is
not the core's to carry, so the gate's content is **a game of its own**: `tests/make-synthetic.py` writes a
four-part "game" - its own bytecode, palettes, polygons, sounds and music - in each release's format, which
goes through the same path that release's files take: a copy-protection part to start at (or the 3DO's logos
and title), an intro with music that goes on by itself, a part the joystick plays (fire's sound, the seed's
marker, the music's marker, the pause, Code) and a password screen. Over it: native == sandbox (every step's
picture, sound, length and lag, the clock, the memory), a savestate before every step, a new host in the middle,
for DOS, the 15th and 20th Anniversary Editions, Windows 3.1, the 3DO folder and the 3DO disc; the 3DO's folder
and disc the same machine; turbo; the settings in both builds; the SoundFont changing Windows 3.1's sound and
nothing else; with `-r`, the DOS effects on the CM-32L (native == sandbox, rerecord, session, and only the sound
changing), and without ROMs, or with files that are not, a refusal; the slot map; six refusals; the two halts in both builds; no host clock in the guest; teeth; and
with `-c` the package through Chimera's own engine (chimera-run). With `-g`, the equivalence, rerecord and
session legs also run on a real release from power-on. `tests/tune.ogg` (the 20th's test music) is the
generator's MIDI tune rendered by TiMidity++ with FluidR3_GM (MIT).

## Where things are

- `waterbox/rawgl-driver.cpp`: the machine - rawgl's `SystemStub` answered by the core, the engine on its own
  stack (`coro.c`), the step, the clock, the input, the picture, the domains and the property table.
- `waterbox/sdl-shim.cpp`, `waterbox/compat/`: the SDL, SDL_mixer and libmt32emu that rawgl's sources ask for:
  the music (WAV, `vorbis.c`, `midi.c`), the channels (AIFF, WAV), the mixing order, resampling.
- `waterbox/detmath.h`: sin, cos, exp, log and pow for the decoders, the same in every build.
- `waterbox/files.c`: the zip, the disc image, the slot map, the SoundFont and the settings. `waterbox/halt.c`: a
  failed assertion halts.
- `waterbox/wbx-entry.c`: the exports. `run-native.c`, `run-wbx.c`, `gate-harness.h`: the harnesses.
- `waterbox/tests/`: the synthetic game and its movie.
- `docs/PLAN.md`: the decisions and what is left.

## Licence

This repository is GPL-2.0-or-later. **rawgl states no licence**: its repository carries no licence file and its
sources name Gregory Montoir's copyright without terms. Its predecessor, raw, is distributed by others under the
GPL (Fabien Sanglard's Another-World-Bytecode-Interpreter is GPL-2.0), but rawgl itself grants nothing in
writing; until its author states terms, a package of this core is for private use and not to be redistributed
(`waterbox/package-licenses.json`). The integration is GPL-2.0-or-later so that it stays compatible with
whichever GPL that turns out to be. miniz (`extern/miniz`) and TinySoundFont (`extern/TinySoundFont`) are MIT,
zlib (`extern/zlib`) is under the zlib licence, stb_vorbis (`extern/stb`) is public domain or MIT, Munt's
libmt32emu (`extern/munt`) is LGPL-2.1-or-later, and libchdr (`extern/libchdr`) is BSD-3-Clause, with the LZMA
SDK's decoder (public domain), Zstandard's (BSD-3-Clause) and dr_flac (public domain or MIT-0) in it.
