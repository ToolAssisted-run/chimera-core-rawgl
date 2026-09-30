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
- **The gate**: 69 legs on the synthetic game in every release's format and every container, including the
  package through chimera-run and (with `-r`) the CM-32L.

## The real DOS release (2026-09-30)

Out of This World, US DOS, a verified copy (GoodOldDays 000568: one 1.2 MB FAT12 disk; the game's files are on
it as they are, no installer packing). Files, for firmware declarations later:

    MEMLIST.BIN 362693989B77FE7CA42F354ABD7162BFED0B8AB2
    BANK01 F4A8186299A94F941BA6EFB567683F31360B6D3C    BANK08 0AE1B6EC1B8A456CD674F650D19E09EFE0CD2987
    BANK02 67C51A2E26577E2032DFDDD02B8D3CB5B127B503    BANK09 1178804D88AF8C8017504C8E225C15991CB52140
    BANK03 8308C482AFB15A0C71518DA30092989DC9F4A8E1    BANK0A A2CBFF889D49708EA44697ACBFB80BD922A8F21F
    BANK04 4CE656819E67653B8B543C7A0CC13E8B7427E217    BANK0B 7A26B6EC3654597A223CB519690ED2A3A6E339B5
    BANK05 DA19759AE2B5326F40A035768C461DBEDE7A9504    BANK0C FB0CEA1A7138B98CBD5177D355E7F39C958A0B95
    BANK06 DB042F61FFFBB97DC3CA4DDB09DB6F2E61F7D942    BANK0D A4CDDF48A160450FEFFF73400F6B1784030727B4
    BANK07 FEDA09078284B134B39F5800220072AFC67D1086

What ran (run-gate.sh -g, with a local movie): the copy protection (answered twice, as it asks; entered with the
joystick like an owner reading the wheel - the movie, and how it was made, stay out of the repo), the credits,
the whole intro (152 s, its waits on VAR_MUSIC_SYNC met by the music player on the machine's time), the first
level (out of the pool, the first screen, the leech's close-up death, the continue screen). 4,040 steps: native
== sandbox, rerecord, session on the first level; the mt32 setting with real CM-32L ROMs, native == sandbox and
only the sound changing. The intro's music reaches the 16-bit ceiling at its loudest: rawgl's mixer clips the
sum of its four channels there, in its SDL build as here.

Found on the way: the protection screen's joystick is slow (the cursor follows a press up to ten frames
later), so a movie that fires too soon selects the wrong symbol - the game's, not the core's.

## The real Amiga release (2026-09-30)

Another World, Amiga, English (archive.org "anotherworldamiga": two 880 KB OFS disks, "Disk1" with bank01, 02,
06, 09, 0B, 0C, 0D and the program, "Disk2" with bank03, 04, 06, 07, 08, 09, 0A, 0C; no bank05 - rawgl's dumper
skips bank 5 on the Amiga and Atari ST, and nothing asks for it). The project's zip is the two disks' files in
one folder. Its banks:

    bank01 57CE8D896B0540C85B8A6E2845A1C2266AE1D296    bank08 A2CE8BBB90CF9EEA9D5C1CB98F58E3D818F3220A
    bank02 202E2E23252493D2305A7C695D04E6C627BDB1CD    bank09 4450576256ECC94FFD5E9B86597FAD6E56C670D3
    bank03 030EBE543A472E3C5398C3230CAED15DD1DB936B    bank0A 63A509ABC1A616C5C5A76A1D2E79F18581E88E04
    bank04 4CE656819E67653B8B543C7A0CC13E8B7427E217    bank0B 9066A5BC9F62240FD6393D9024BA3F45A5914ADA
    bank06 93A704C78DBE6FEACCC99E618D3F1CB7AE6C0D8F    bank0C 93309C022BE0F74064BF31618F8433B1C4D093DC
    bank07 B1032105D29CE5A7D504FFC845BECE4ADD203BAB    bank0D D42EB7DF5C912D322B24BFB23660AF36972668B6

What ran (run-gate.sh -g -M, a local movie): the copy protection (the same screen as DOS - the first 400 steps
are the DOS run's to the pixel and the sample - answered twice), the credits, the whole intro (148 s: the
Amiga's music runs a little shorter than the DOS port's; the same sync points), the first level to the leech.
3,767 steps: native == sandbox, rerecord, session; and through chimera-run. The picker's cursor follows a
press up to 25 frames late here (10 on DOS): the local solver now waits for the game to act instead of counting
frames.

## The real Atari ST release (2026-09-30)

Another World, Atari ST, TOSEC 2011 "(FR)(Disk 1 of 2)[!][protected]" and "Disk 2": Pasti (.stx) images, whose
ordinary 512-byte sectors (read with a local Pasti reader, from the format as Hatari reads it) make two
complete 720 KB FAT12 disks; the copy protection is on tracks 80-81, past the file system. Disk 1: BANK01, 02,
06, 09, 0B, 0D and the program; disk 2: BANK03, 04, 06, 07, 08, 09, 0A, 0C (06 and 09 the same on both; no
05). BANK01 is 227,142 bytes, rawgl's Atari ST size (its only one - rawgl calls the table "EN"; these disks'
banks are what it expects, whatever TOSEC's language tag). Banks:

    BANK01 F18966A716E0CB37FEE8AA1574111C9870F25EC8    BANK08 D495CD00B3802453F881854778CA13036D97D133
    BANK02 11F3A911E7DF3FEF1136CBBD6A814C146E19134A    BANK09 4450576256ECC94FFD5E9B86597FAD6E56C670D3
    BANK03 030EBE543A472E3C5398C3230CAED15DD1DB936B    BANK0A 63A509ABC1A616C5C5A76A1D2E79F18581E88E04
    BANK04 6A9E0913C2130F93BD606F1F8831024E460D3579    BANK0B 9C14D8ED539271D2801CA0C092B4000C0F2DD2D3
    BANK06 93A704C78DBE6FEACCC99E618D3F1CB7AE6C0D8F    BANK0C 93309C022BE0F74064BF31618F8433B1C4D093DC
    BANK07 E9C98F08A6C446D19E4466218E8249AD8252F7EE    BANK0D D42EB7DF5C912D322B24BFB23660AF36972668B6

Ran: the protection (answered twice), the credits, the intro, the first level to the leech; 3,767 steps
native == sandbox, rerecord, session; chimera-run. Against the Amiga's run with the same inputs: every picture
and every step's length the same over 4,600 steps (so rawgl's Atari bitmap decoding gives what its Amiga one
does, where this span shows bitmaps), the sound its own.

A project brings the banks as a zip. The disks themselves (.st, .msa, .stx; the Amiga's .adf; the DOS
floppy's .img) are not taken: that would be a slot of several disks and a reader for each format in the core.

## The releases as they come (2026-09-30)

Sergio: "accept the disk images directly - same for the other disk-based platforms; same for 3DO: accept CHD
directly". The game slot now takes several files, of any mix: zips, floppy images, and zips of floppy images
(TOSEC's). `disks.c` reads a floppy's files: FAT12/16 (the DOS PC's .img/.ima, the Atari ST's .st - its boot
sector has no 0x55AA, so the parameter block's numbers are the test), MSA (its run-length packing), Pasti
(.stx: the ordinary sectors - 512 bytes, the track's own number, with data - into a raw image of the boot
sector's geometry; the protection's odd sectors left out), AmigaDOS (.adf: OFS or FFS, the root in the middle
of the disk, the hash chains). Every disk's files join one list, the first of a name kept (the Amiga's and the
ST's two disks share banks, byte for byte); the game's folder is found in it as in a zip, and only its files are
unpacked. A disc (.chd, .iso) is the 3DO's and comes alone. Not taken: 7-Zip archives (the DOS copy came as a
.7z holding its .img: the .img is what a project brings) and other disk formats (IPF, DMS, raw flux).

Checked: the real disks read directly are the same machines as their files zipped (DOS .img, Amiga .adf x2,
Atari .stx x2 bare and in TOSEC's zips) - native == sandbox, rerecord, session - and load as real Chimera
projects through chimera-run --project, the 3DO's .chd too. The synthetic DOS game is written on every format
(a 1.44 MB .img; two .adf, .st, .msa, .stx with a protected track; a zip of the two .adf), each read with an
independent tool as a check of the writer (mtools, the local Python readers), each the same machine as its zip.

## The real 3DO release (2026-09-30)

Out of This World (USA), 3DO, as a CHD (v5, codecs cdlz/cdzl/cdfl, one MODE1_RAW track of 164,290 frames:
216 MB for 336 MB of disc). rawgl reads a 3DO disc only as an Opera image or a GameData folder, and 3DO discs
mostly come as CHDs, so the core reads one itself: libchdr (extern/libchdr, v0.3.0 - the release, not its
development head), built against the core's zlib (CHDR_SYSTEM_ZLIB, so its bundled miniz does not meet the
core's), its LZMA decoder, Zstandard's decoder with its run-time BMI2 dispatch off and dr_flac with its SIMD
off (the same code on every CPU). It opens the file through callbacks that open, read and close the mount at
each call (as the .iso path does: miniBox's savestates carry no host handles), finds the first MODE1 or
MODE1_RAW track (chdman's layout: tracks padded to 4 frames, a stored pregap first), and serves its 2048-byte
sectors to rawgl as game.iso, one decompressed hunk (8 frames) cached in guest memory. Checked: the first
sector must be an Opera file system's, or the project is refused as "a CD, but not a 3DO disc".

What ran (run-gate.sh -g -M, a local movie): the three logos (the 3DO's songs under them), the title, the
160-second intro (60 Hz timings: steps of 16, 33, 66, 83 ms), the first level to the leech and the 3DO's continue
screen. 3,907 steps: native == sandbox, rerecord, session; chimera-run with the .chd as its rom. The synthetic 3DO
disc is also written as an (uncompressed) CHD, and the gate holds it to the same machine as the folder and the
image.

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
  pad's Jump as a button only it has (`IsButtonActive`). Its Back key is not a button at all (below).
- **Settings**: difficulty (the 20th's) and remasteredAudio (the anniversary editions'), which rawgl hands to the
  20th's script (variables 0xBF, 0xDE) and uses to pick sound files.

- **The MT-32** (Sergio: "all MT-32 support"): rawgl's `--mt32`, the DOS release's sound effects on a
  Roland CM-32L - Munt's libmt32emu at the SDLPoP2 core's commit (2.8.3), compiled into both builds in place of
  the stub, the mt32 setting, the CM-32L's ROMs as firmware required with it (v1.02 control, the 1 MB PCM; the
  hashes are Munt's own table's). rawgl opens them by name itself (`mt32emu_add_rom_file`, Munt's ifstream,
  which works on miniBox's mounts); the driver tries each in a context of its own first, so a missing file or
  one that is not a ROM is refused by name instead of playing silence. Tested with real CM-32L ROMs on the
  synthetic game (its fire sound is 0x33, one rawgl maps to a rhythm note): native == sandbox, rerecord,
  session, only the sound changing; and through chimera-run with the ROMs as firmware. Only DOS: rawgl uses it
  for no other release.

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
  Resource::setupPart stops with "invalid part" (the 3DO has no part 16000). Upstream's SDL build exits there.
  Sergio's decision (2026-09-30): the menu is not offered at all - there is no Back button, rawgl's `back` flag
  is never set, so the menu cannot open and the bug cannot be reached. rawgl is unchanged.
- **The 3DO's ending, probably the same** (read, not run): the end credits are asked for as part 16009, which
  Engine::run also leaves pending; after the credits and the title, setupTasks would load 16009 - a real
  number, the password screen - instead of the part chosen. To see with the real 3DO data.
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
