# chimera-core-rawgl

[rawgl](https://github.com/cyxx/rawgl), Gregory Montoir's reimplementation of the engine of Another World
(Out of This World; Delphine Software, Eric Chahi, 1991), as a [Chimera](https://github.com/ToolAssisted-run/chimera)
**game core** (`"kind": "game"`, see Chimera's `docs/game-cores.md`): the game's bytecode, its polygons, its
sound channels and music player, stepped one frame of the game at a time in miniBox's sandbox, packaged as
`rawgl.chimeraCore`.

**Built on upstream rawgl, with three patches**: the first serves the engine's file opens (and its `stat()`)
from the project's file, the second hands a fatal error to the core before rawgl would exit, the third lets
rawgl read two anniversary-edition installations it did not know (GOG's 20th, the 15th's European CD). Everything else is
rawgl compiled from source - its script interpreter, its software renderer, its mixer (compiled as it is,
against an SDL_mixer of the core's) - without its SDL and OpenGL frontend, which the core is instead. The
core's SDL_mixer plays what rawgl asks of it with miniz, zlib, stb_vorbis and TinySoundFont, over its own
deterministic math (`waterbox/detmath.h`), so the sandbox and the native reference make the same sound, and
the CM-32L is Munt's.

## What it is

- **Every release rawgl plays is a machine** - Chimera's System list (`waterbox.config` "machines", all of them
  the `AnotherWorld` system; the release setting is what a project records) - and **its files are the
  release's firmware**, the game's own files, never an installer. The wizard has no files step: the System
  picks the release, the settings page shows that release's settings, and the firmware page asks for its files:

  | System (release setting) | Its files (firmware id) |
  |---|---|
  | Another World (DOS) (`dos`) | the disk image, `dos-disk` (.img/.ima, or a zip of MEMLIST.BIN and the BANK files) |
  | Another World (Amiga, English) (`amiga`) | the two disks, `amiga-en-disk1`, `amiga-en-disk2` (.adf, or TOSEC's zips) |
  | Another World (Amiga, French) (`amigafr`) | the two disks, `amiga-fr-disk1`, `amiga-fr-disk2` |
  | Another World (Atari ST) (`atari`) | the two disks, `atari-disk1`, `atari-disk2` (.st, .msa, Pasti's .stx, or TOSEC's zips) |
  | Out of This World (Windows 3.1) (`win31`) | `BANK`, `WORLD.EXE`, `X.MID`, `Y.MID` |
  | Out of This World (3DO) (`3do`) | the disc, `3do-disc` (.iso, or MAME's .chd) |
  | Another World 15th Anniversary Edition (`15th`) | `Pak01.pak`, `Intro2004.ogg`, `End2004.ogg`, and the language's `lang_English.Txt`, `lang_Francais.Txt` or `lang_Espanol.txt` |
  | Another World 20th Anniversary Edition (`20th`) | a zip of its game folder, `20th-game` |
  | Out of This World (DOS demo) (`dosdemo`) | `dos-demo` (ootwdemo.zip as it is distributed) |
  | Another World (Atari ST rolling demo) (`stdemo`) | `atari-demo-disk` (ST Action's cover disk 28, .st or .stx) |

  Each entry pins the hash of the dump it was tested with, so the wizard's folder scan finds it; a file of the
  user's own is taken too, and the project pins its hash. The settings follow the release: Difficulty is the
  20th's, Remastered Sound the anniversary editions', MT-32 Sound Effects the DOS release's (and its demo's),
  SoundFont Music Windows 3.1's, Language the releases that have texts of their own. A setting left on from
  another release counts for nothing (the CM-32L's ROMs and the SoundFont are asked for only on their
  releases).

  The core reads each file as it is (`waterbox/files.c`): a floppy's image (`waterbox/disks.c`: FAT, .msa,
  Pasti's .stx, AmigaDOS OFS and FFS - the core takes only the files; the protections are on tracks past the
  file systems), a zip (and the disk images in it), the 3DO's disc (a .chd through libchdr: its first data
  track is what rawgl reads as the disc), or a file of the game's, which goes where the game's folder has it.
  A game on two disks is one folder to rawgl. The 15th's CD and GOG's 20th ship the game inside installers
  (NSIS, Inno Setup), which the core does not open: an installer, and the 15th's PC CD, are refused, naming the
  files to take out of them instead. A host that is no project's - chimera-run given a rom, the core's
  harnesses - may still bring the game as one file, or a "game" slot of several, as before.

  The core unpacks the files into sealed memory at start (no savestate carries them) and reads a disc where it
  lies, a block at a time, holding nothing of the host's between two reads. The package
  carries none of the game's data. A release's file missing, a release the core does not have, a file of none of
  the kinds above, and data rawgl cannot tell are refused, saying why.

  | Release | The folder is recognised by | Starts at | Sound | Picture |
  |---|---|---|---|---|
  | DOS | MEMLIST.BIN + BANK01..BANK0D | the copy protection (the demo: the intro) | rawgl's 4 channels and module player; the effects on a CM-32L with the mt32 setting | original, 320x200 |
  | DOS demo | MEMLIST.BIN + BANK01, 02, 05, 06, 0D (or DEMO01..) | the intro, then the first level | the same | the same |
  | Amiga (French, English) | BANK01 of 244,674 / 244,868 bytes | the copy protection | the same | the same |
  | Atari ST | BANK01 of 227,142 bytes | the copy protection | the same | the same |
  | Atari ST demo | AW.TOS of 96,513 bytes | the intro | the same | the same |
  | 15th Anniversary Edition | Data/Pak01.pak | the intro | WAV sounds, WAV music (original or remastered) | original, 320x200 (see below) |
  | 20th Anniversary Edition | game/DAT/FILE017.DAT | the intro | gzip'd WAV sounds, Ogg Vorbis music (original or remastered) | original, 320x200 (see below) |
  | Windows 3.1 | BANK (+ WORLD.EXE) | the copy protection | WAV sounds, MIDI music (with a SoundFont, below) | original, 320x200 |
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
- **Settings**: Release (the System, which the wizard's settings page does not show), Language, Random seed
  (0..65535), Difficulty (the 20th's, rawgl's `--difficulty`),
  Remastered Sound (the anniversary editions', rawgl's `--audio`), MT-32 Sound Effects (the DOS release's,
  rawgl's `--mt32`) and SoundFont Music (Windows 3.1's).
- **The MT-32**: with the mt32 setting, rawgl sends the DOS release's sound effects it has a note for (41 of
  them) to a Roland CM-32L's rhythm part instead of playing the game's samples - Munt's libmt32emu, compiled in,
  with the CM-32L's ROMs the project brings as firmware (`CM32L_CONTROL.ROM`, `CM32L_PCM.ROM`; another pair Munt
  knows may take their place). The music stays the game's own. A project with the setting and without the ROMs,
  or with files that are not ROMs, is refused, naming them.
- **The SoundFont**: Windows 3.1's music is two General MIDI files. With the soundFont setting, TinySoundFont
  plays them with a General MIDI SoundFont of the user's choice, which the project brings as firmware
  (`soundfont.sf2`; no hash pinned - the project records the one chosen). Without the setting the music is
  silent, as rawgl without a MIDI synthesizer; the game and its timing are the same either way. A project with
  the setting and without the file, or with a file that is not a SoundFont, is refused.
- **Properties** (Chimera's `docs/game-cores.md`): a `Game State` block (the part, the part to come, the
  screen, the release, the language, the machine's steps and milliseconds, whether the music plays, whether
  the engine halted) and the game's **256 script variables** in place (`Script Variables`, writable): the
  engine's named ones (`VAR_HERO_*`, `VAR_MUSIC_SYNC`, `VAR_PAUSE_SLICES`...), Lester's and the world's under
  the names JaffarPlus's Another World gives them, and all 256 as `Var[0..255]`.
- **A fault in the data halts the machine, not the frontend**: rawgl's `error()` and its failed assertions stop
  the engine where it stands with the reason; the machine keeps stepping, silent, at 50 Hz, with
  `Machine.Halted` set.

## What has been run

**Every release has run on real data** (below): DOS and its demo, Amiga (English and French), Atari ST and its
demo, 3DO, the 15th and 20th Anniversary Editions and Windows 3.1. Every
release has also run as the core's **synthetic game** - its own bytecode, pictures, sounds and music written in that release's
format (`waterbox/tests/make-synthetic.py`) - through the whole path its files take: native == sandbox,
rerecord, session. That proves the core's side (the files, the step, the clock, the sound decoders, the
savestates) on each release's formats; it does not prove the releases' own content (their scripts, their
compressed resources - the DOS banks' ByteKiller packing, Windows 3.1's LZ-Huffman, the 3DO's LZSS and coded
cels as the real files use them, the 15th's TooDC encoding - their copy protections, their timing). `run-gate.sh
-g <file> [-g <file>...] [-M <movie>]` runs the equivalence, rerecord and session legs on a real release, from
its files as a project brings them.

The real releases below have also run from their disks' images as they came - the DOS floppy's .img, the two
Amiga .adf, the two Atari ST Pasti images (bare, and in TOSEC's zips), the 3DO's .chd - each the same machine
as its files zipped, native == sandbox, rerecord, session; and as real Chimera projects (chimera-run
--project), the engine checking the files against the core's slot.

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

**Another World (Atari ST: TOSEC's two Pasti disk images, tagged FR)**: the disks' ordinary sectors hold a
FAT12 file system with the banks (the protection lives on the tracks past it); merged into one folder as on the
Amiga, rawgl knows the release by BANK01's size (227,142 bytes, the one Atari ST size it has; its texts are
rawgl's own, per the language setting). The same run as the Amiga's - 3,767 steps, native == sandbox, rerecord,
session - and the same machine to the picture and the step: the whole 4,600-step run's pictures and step
lengths are the Amiga run's; only the sound is its own (the ST's samples).

**Another World, 15th Anniversary Edition (Europe, En/Fr/Es: a Redump CD image)**: the CD holds the game
inside an NSIS installer (`AnotherWorld_full.exe`: `Data/Pak01.pak`, the music, `Menu/lang_*.Txt`), which a
user installs, or opens with 7-Zip; the project brings the four files loose (or zipped) - the same machine as
the whole installed folder zipped. The game runs without the texts.
The original renderer; the intro (153 s) with its Ogg music (patch 0003), the first level to the leech and the
edition's own continue screen - 3,097 steps: native == sandbox, rerecord, session. The pack has no sound 52,
which the game asks for three times in the intro: silent, as rawgl warns.

**Another World, 20th Anniversary Edition (GOG, 2.0)**: GOG's Inno Setup installer, opened (innoextract) and
zipped; rawgl reads its layout with patch 0003. The original renderer draws the game's polygons and its 320x200
pictures; the HD backgrounds (1728x1080 here) are not unpacked. The intro (153 s) with its Ogg music, the first
level to the leech - 3,097 steps: native == sandbox, rerecord, session; the difficulty and remastered settings
reach the script as with the synthetic game.

**Out of This World, Windows 3.1**: the installed folder, zipped (BANK, WORLD.EXE, the two MIDI files). The copy
protection (answered, the same screen), the intro (152 s) with its MIDI music, played by TinySoundFont with a
General MIDI SoundFont (FluidR3 GM, the test's; any the project brings), the first level to the leech -
native == sandbox, rerecord, session. MIDI costs: with a 148 MB General MIDI SoundFont the intro renders about
six times faster than it plays, against ninety without music - fine to play, slower to seek through.

**Another World (Amiga, French: TOSEC's "(FR)(Disk 1 of 2)[cp code wheel]" and "(Disk 2 of 2)", each a zipped
.adf)**: the two zips as they come. rawgl knows the release by BANK01 (244,674 bytes). The copy protection
(answered twice), the whole intro, the first level - 3,980 steps: native == sandbox, rerecord, session. The
game's texts come from rawgl's tables by the language setting: French with "fr", the steps and the sound the
same.

**Out of This World, DOS demo (Interplay, 1992: `ootwdemo.zip`, archive.org's "OutOfThisWorldDemo")**: the
zip as it is (MEMLIST.BIN, BANK01, 02, 05, 06 and 0D, DEMO3.JOY, the demo's own programs). No copy protection:
the intro, then the first level (the pool, the first screens) until Lester dies - 3,640 steps: native ==
sandbox, rerecord, session. DEMO3.JOY holds the demo's recorded inputs, which rawgl plays with its
`--demo3-joy` option in place of the player's; the core leaves it off - the player plays.

**Another World, Atari ST rolling demo (ST Action issue 44, December 1991, cover disk 28: archive.org's "ST
Action (UK) Magazine Coverdisks")**: the disk's image as it is - the .st or the Pasti .stx, the same machine -
holds AW.TOS (96,513 bytes, the size rawgl knows it by) beside two other demos. The intro, to the lightning in
the accelerator; then the last picture holds, as upstream rawgl has it (the demo carries no more of the game) -
3,200 steps: native == sandbox, rerecord, session.

**Out of This World (3DO, USA: a CHD, MAME's compressed image - one MODE1_RAW track of 164,290 frames)**: the
project brings the .chd as it is; libchdr reads it (LZMA, zlib and FLAC hunks), a hunk at a time, through
callbacks that open, read and close the mounted file, so a savestate holds nothing of the host's. The Interplay
logo and the others with the 3DO's songs, the title, the 160-second intro at the 3DO's 60 Hz timings with its
painted backgrounds, the first level to the leech and the 3DO's own continue screen - 3,907 steps: native ==
sandbox, rerecord, session; and chimera-run taking the .chd as its rom.

| Release | Synthetic game | Real files |
|---|---|---|
| DOS | yes, and its effects on a CM-32L (with real CM-32L ROMs) | **yes**: Out of This World (US), the copy protection answered, its credits, the whole intro, the first level; native == sandbox, rerecord, session; and with the CM-32L |
| DOS demo | (the DOS path, less the password screen) | **yes**: Interplay's 1992 demo (`ootwdemo.zip`), as it comes; the intro, the first level; native == sandbox, rerecord, session |
| Amiga English | no (rawgl finds its resources by a built-in table keyed on BANK01's size) | **yes**: the two disks' files, the copy protection answered, the whole intro, the first level; native == sandbox, rerecord, session |
| Amiga French | no (the same) | **yes**: TOSEC's two zipped .adf as they are, the copy protection answered, the whole intro, the first level; native == sandbox, rerecord, session; its texts in French with the language setting |
| Atari ST | no (the same) | **yes**: the two disks' files (TOSEC's Pasti images, "FR"), the copy protection answered, the whole intro, the first level; native == sandbox, rerecord, session |
| Atari ST demo | no (the same, AW.TOS) | **yes**: ST Action's cover disk 28 (issue 44, December 1991), its .st or .stx as it is; the rolling intro; native == sandbox, rerecord, session |
| 15th Anniversary Edition | yes: Pak01.pak, WAV sounds and music, original and remastered | **yes**: the European CD's installation; the whole intro with its music, the first level; native == sandbox, rerecord, session |
| 20th Anniversary Edition | yes: game/, gzip'd sounds, Ogg music, difficulty, original and remastered | **yes**: GOG's 2.0; the whole intro with its music, the first level; native == sandbox, rerecord, session |
| Windows 3.1 | yes: BANK (unpacked entries), its palettes, WAV, MIDI with a SoundFont | **yes**: its folder, zipped; the copy protection answered, the intro with its MIDI music (a General MIDI SoundFont), the first level; native == sandbox, rerecord, session |
| 3DO | yes: GameData/, a disc image and a CHD, logos and title, AIFF, SDX2, a coded cel | **yes**: Out of This World (USA), a CHD; the logos, the title, the whole intro, the first level; native == sandbox, rerecord, session; and through chimera-run |

## The patches

- `0001-memory-files.patch`: under `RAWGL_MEMFS`, rawgl's `File` opens through the core - `rawgl_memfs_find()`,
  the zip's files in memory, or `rawgl_memfs_host_read()`, a disc image read by position - its `stat()` asks
  the core too, a read past a file's end gives zeros (rawgl's `File::readByte` leaves its byte uninitialised
  otherwise, and the 3DO's song player reads past each song), and its case-insensitive directory walk is not
  built.
- `0002-error-hook.patch`: `error()` calls a weak `rawgl_error_hook()` before it exits; the core's halts the
  machine instead.
- `0003-anniversary-layouts.patch`: rawgl's 20th Anniversary reader knows the Linux/Steam layout (gzip'd
  pictures in `game/BGZ`, texts in `game/TXT` or `game/TXT/Linux`); GOG's installation keeps plain `.bmp`
  pictures in `game/BMP` and its texts in `game/TXT/Win32`, and rawgl now reads those too. Its 15th
  Anniversary reader looks for the intro's and the end's music as `Music/AW/*.wav`; the European CD installs
  them as `Music/Intro2004.ogg` and `Music/End2004.ogg`, which rawgl now falls back to. Additions only: the
  layouts rawgl knew read as before.

## Using it in Chimera

Chimera ships no cores and downloads nothing. Download the core's `.chimeraCore` package from this repository's
[Releases](https://github.com/ToolAssisted-run/chimera-core-rawgl/releases) page (a rolling `dev` build, dated
`nightly-YYYY-MM-DD` builds), or build it, and put it in the `Cores` folder beside `Chimera.exe` (or the folder
chosen in File > Core Manager > Change folder...). The same file works on Linux and on Windows. The game's own
files are not in the package: a project brings its release's files as firmware (the table above).

## Building

```
git submodule update --init
make -C waterbox -f native.mk -j$(nproc)    # the native reference and the harnesses
make -C waterbox -f guest.mk -j$(nproc)     # core.wbx
./waterbox/build-package.sh                 # build/package/rawgl.chimeraCore
```

miniBox is taken from `MB=`/`MINIBOX_DIR` (`-m` for the scripts), else `~/chimera/extern/chimera-common-minibox`,
with its C++ guest toolchain built (`meson setup build/meson-cpp -Dguest_cpp=true`). The patches go onto
`extern/rawgl` on the first build (`waterbox/apply-patches.sh`, all or nothing). `./waterbox/build-package.sh -r
<chimera>` writes the package into a Chimera source checkout's `build/Cores` instead.

The whole build, as CI does it, is in [docs/BUILDING.md](docs/BUILDING.md); [AGENTS.md](AGENTS.md) is the guide
for an AI coding agent.

## The gate

`./waterbox/run-gate.sh [-m <miniBox>] [-c <chimera-run>] [-g <Another World zip or iso>] [-r <ROM dir>]`. Another World's data is
not the core's to carry, so the gate's content is **a game of its own**: `tests/make-synthetic.py` writes a
four-part "game" - its own bytecode, palettes, polygons, sounds and music - in each release's format, which
goes through the same path that release's files take: a copy-protection part to start at (or the 3DO's logos
and title), an intro with music that goes on by itself, a part the joystick plays (fire's sound, the seed's
marker, the music's marker, the pause, Code) and a password screen. Over it: native == sandbox (every step's
picture, sound, length and lag, the clock, the memory), a savestate before every step, a new host in the middle,
for DOS, the 15th and 20th Anniversary Editions, Windows 3.1, the 3DO folder, image and CHD; the 3DO's folder,
image and CHD the same machine; the DOS game on every floppy format the core reads (a DOS .img, two .adf, two
.st, .msa and .stx, a zip of two .adf) the same machine as its zip, native == sandbox; turbo; the settings in both builds; the SoundFont changing Windows 3.1's sound and
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
- `waterbox/files.c`: the project's files (zips, disks, discs), the slot map, the SoundFont and the settings.
  `waterbox/disks.c`: the floppy images' file systems (FAT, AmigaDOS) and containers (MSA, Pasti). `waterbox/halt.c`: a
  failed assertion halts.
- `waterbox/wbx-entry.c`: the exports. `run-native.c`, `run-wbx.c`, `gate-harness.h`: the harnesses.
- `waterbox/tests/`: the synthetic game and its movie.
- `docs/PLAN.md`: the decisions and what is left.

## Licence

This repository is GPL-2.0-or-later. **rawgl states no licence**: its repository carries no licence file and its
sources name Gregory Montoir's copyright without terms. Its predecessor, raw, is distributed by others under the
GPL (Fabien Sanglard's Another-World-Bytecode-Interpreter is GPL-2.0), but rawgl itself grants nothing in
writing. This core's packages carry rawgl compiled from its public repository, in good faith, for players of
the original game, and will be taken down should its author object (`waterbox/package-licenses.json`). The
integration is GPL-2.0-or-later so that it stays compatible with whichever GPL rawgl's terms turn out to be. miniz (`extern/miniz`) and TinySoundFont (`extern/TinySoundFont`) are MIT,
zlib (`extern/zlib`) is under the zlib licence, stb_vorbis (`extern/stb`) is public domain or MIT, Munt's
libmt32emu (`extern/munt`) is LGPL-2.1-or-later, and libchdr (`extern/libchdr`) is BSD-3-Clause, with the LZMA
SDK's decoder (public domain), Zstandard's (BSD-3-Clause) and dr_flac (public domain or MIT-0) in it.
