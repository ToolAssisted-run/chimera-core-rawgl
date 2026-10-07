# Building the rawgl core

This repository builds rawgl, the reimplementation of the engine of Another
World (Out of This World), as a Chimera game core: one game engine built as a
core, not an emulator (Chimera's `docs/game-cores.md`). The result is one file,
`rawgl.chimeraCore`, which Chimera loads. The steps below follow
`.github/workflows/chimera.yml`, which builds and gates the core from a fresh
clone on a public Ubuntu runner.

Placeholders used below:

- `<core>`: the checkout of this repository.
- `<chimera>`: a checkout of Chimera (https://github.com/ToolAssisted-run/chimera).
- `<minibox>`: `<chimera>/extern/chimera-common-minibox`, Chimera's miniBox
  submodule (the sandbox host and the guest toolchain).

## Requirements

- Linux. CI runs on GitHub's `ubuntu-latest` runner. Cores are built on Linux;
  the package they produce runs on Linux and on Windows.
- The apt packages the workflow installs:

  ```
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends meson ninja-build build-essential cmake pkg-config python3 mono-complete xvfb libgl1-mesa-dev libegl-dev libx11-dev libxext-dev libasound2-dev python3-pip
  ```

- The .NET SDK 8.0 (the workflow uses `actions/setup-dotnet@v4` with
  `dotnet-version: '8.0'`). It is needed to build the Chimera solution and to
  run Chimera's contract tests, not to build the package. Chimera's README
  installs it with
  `curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0`.
- The compilers are the system's `gcc` and `g++` (from `build-essential`). The
  workflow pins no compiler version. The guest is compiled by the same `gcc`
  and `g++` over miniBox's guest sysroot (`waterbox/guest.mk`).
- No Rust toolchain. The workflow installs no Python packages with pip.
- Nothing is downloaded by this repository's scripts. The gate writes its own
  test content (`waterbox/tests/make-synthetic.py`).

## Get the sources

The workflow checks out both repositories with their submodules, recursively
(`actions/checkout@v6`, `submodules: recursive`), and takes Chimera at `main`.
By hand:

```
git clone --recursive https://github.com/ToolAssisted-run/chimera-core-rawgl.git <core>
git clone --recursive https://github.com/ToolAssisted-run/chimera.git <chimera>
```

In a clone made without `--recursive`:

```
git -C <core> submodule update --init --recursive
git -C <chimera> submodule update --init --recursive
```

This repository's submodules are all under `extern/` (`.gitmodules`): `rawgl`
(the engine), `miniz`, `zlib`, `stb`, `TinySoundFont`, `munt` and `libchdr`.

CI puts the Chimera checkout at `<core>/chimera-checkout`. Any place works:
the scripts find miniBox in this order.

1. `-m <miniBox dir>` on `run-gate.sh` and `build-package.sh`, or `MB=<dir>`
   on the `make` command line.
2. The `MINIBOX_DIR` environment variable (what CI sets).
3. For `build-package.sh -r <chimera>`: `<chimera>/extern/chimera-common-minibox`.
4. `~/chimera/extern/chimera-common-minibox`.

## Build miniBox

The workflow builds Chimera itself first, then miniBox. Chimera's own build
gives `<chimera>/build/dll/chimera-run` (used by the gate's engine leg) and the
managed solution (used by the contract tests). The package does not need it:
`build-package.sh` uses only miniBox.

```
cd <chimera>
meson setup build/meson-linux --prefix "$PWD/build" --libdir dll
meson compile -C build/meson-linux
meson install -C build/meson-linux
dotnet build source/gui/Chimera.sln -c Release /nodeReuse:false -p:UseSharedCompilation=false
```

Then miniBox: the host, and the guest toolchain with C++ (rawgl and Munt are
C++).

```
mb=<chimera>/extern/chimera-common-minibox
[ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
meson compile -C "$mb/build/meson-linux"
[ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
meson compile -C "$mb/build/meson-cpp"
```

- `build/meson-linux` holds the miniBox host library
  (`source/host/libminiboxhost.so`), which the `run-wbx` harness links.
- `build/meson-cpp` holds the guest sysroot (`guest-sysroot`: musl and
  libstdc++) and the guest objects `guest.mk` links.

CI caches these two build directories (`actions/cache@v4`). By hand there is
nothing to do: the directories stay where they are, and the `[ -f ... ] ||`
guards skip `meson setup` when one is already configured.

## Build the core

`run-gate.sh` and `build-package.sh` run the builds themselves, so this step
is only needed to build without gating or packaging.

```
cd <core>
make -C waterbox -f native.mk MB=<minibox> -j"$(nproc)"
make -C waterbox -f guest.mk MB=<minibox> -j"$(nproc)"
```

- **Patches.** `patches/` holds three numbered patches for `extern/rawgl`.
  Both makefiles apply them first, through `waterbox/apply-patches.sh`
  (`build/patches.stamp` records that it ran). The script judges the series as
  a whole. A pristine submodule gets every patch. A tree that already carries
  the whole series is left alone. Anything in between is an error that names
  the files and prints the command to start again. The series is first tried on
  a scratch copy, so a series that does not apply never touches the tree. The
  script can also be run by hand: `./waterbox/apply-patches.sh`.
- **The guest core** (`guest.mk`): `build/guest/core.wbx`. It is rawgl, Munt,
  the decoders and the core's own sources, built with the system compilers
  over miniBox's guest sysroot. miniBox's `check-wbx.sh` checks the result (no
  thread-local storage, no `%fs`, no red zone) before it counts as built.
- **The native reference** (`native.mk`): `build/native/run-native` and
  `build/native/run-wbx`. `run-native` is the same sources built for the host
  with no sandbox. `run-wbx` drives `core.wbx` through the miniBox host as the
  frontend does. The gate compares the two step by step. The native reference
  is not part of the package.

`sources.mk` is included by both makefiles, so both build the same files with
the same defines. Each object depends on the flags it was built with: a change
of flags rebuilds it. `make -C waterbox -f guest.mk clean` and
`make -C waterbox -f native.mk clean` remove `build/guest` and `build/native`.

## Build the package

```
cd <core>
./waterbox/build-package.sh -r <chimera>
```

`waterbox/build-package.sh [-m <miniBox dir>] [-r <chimera root>] [-o <out dir>]`:

- `-r <chimera root>` writes `<chimera root>/build/Cores/rawgl.chimeraCore`
  and takes miniBox from that checkout unless `-m` or `MINIBOX_DIR` names
  another. It also removes `<chimera root>/build/CoreCache/rawgl-*`. This is
  what CI runs (`-r chimera-checkout`).
- `-o <out dir>` writes `<out dir>/rawgl.chimeraCore` instead.
- With neither, the package is `<core>/build/package/rawgl.chimeraCore`.

The script builds the guest (`guest.mk`; the log is `build/package-make.log`),
checks `core.wbx`, and packs `core.wbx`, `waterbox.config`,
`default_keybinds.json`, `file_slots.json`, the licence texts and a
`build.json` that records what built the package. The packing is
deterministic: the script packs twice and stops if the two SHA-1s differ, then
prints `package sha1 <hash>`.

The version is the commit the package was built from.

- CI sets `CORE_VERSION` to the commit's full hash.
- By hand, with `CORE_VERSION` unset, the script stamps `<commit>+local`
  (twelve hex digits), or `<commit>-dirty+local` when the tree has changes.
  The patches applied inside `extern/rawgl` do not count as changes.

A hand-built package is for testing. Chimera's publish script refuses a
version that carries `+local` or `-dirty`.

CI publishes what its gate passed as this repository's own releases: a rolling
`dev` release on every green push to `main`, and a dated `nightly-YYYY-MM-DD`
release from the scheduled run (04:00 UTC, only when `main` moved since the
last one). The gate job uploads the package as the artifact `rawgl-<commit>`
(`actions/upload-artifact@v7`), and the publish job hands it to Chimera's
reusable workflow `publish-core.yml`. There is no manual equivalent.

## Install it into Chimera

Chimera ships no cores and downloads nothing: it has no network code. A core
gets into Chimera because somebody put its package file in the cores folder.

- **A release bundle of Chimera**: download the core's `.chimeraCore` package
  from this repository's Releases page
  (https://github.com/ToolAssisted-run/chimera-core-rawgl/releases), or build
  it, and put it in the `Cores` folder beside `Chimera.exe`. Another folder can
  be chosen in File > Core Manager > Change folder...
- **A Chimera source checkout**: the cores folder is `<chimera>/build/Cores/`.
  `./waterbox/build-package.sh -r <chimera>` writes the package straight there.

File > Core Manager lists what is in the folder. Refresh List rescans it. The
same package file works on Linux and on Windows: the guest inside it is run by
Chimera's sandbox (miniBox) on either.

## Run the gates

### The core gate

```
cd <core>
MINIBOX_DIR=<minibox> ./waterbox/run-gate.sh -c <chimera>/build/dll/chimera-run
```

This is the command CI runs. The gate builds the native reference and
`core.wbx` first (logs: `build/gate-native.log`, `build/gate-guest.log`),
works in `build/gate` (emptied at each run), prints `PASS` or `FAIL` for every
leg, and ends with `gate: all legs passed` or `gate: N leg(s) failed` and a
non-zero exit.

It proves that the native reference and the sandboxed core are the same
machine. The game's files are never in the repository, so the content is a
synthetic game the gate writes in each release's format. The legs, as the
script's header lists them: equivalence (every step's picture, sound, length
and lag, the clock, both memory domains), rerecord (a savestate saved and
loaded before every step), session (saved, a new host, loaded, finished),
turbo, releases, disks, loose, settings, firmware, slots, mt32, refusals,
halts, clock (the guest has no `time()` of its own), teeth (a movie one step
different must digest differently) and engine.

Usage: `run-gate.sh [-m <miniBox dir>] [-g [<id>=]<file>]... [-R <release>] [-M <movie>] [-f <frames>] [-c <chimera-run>] [-r <ROM dir>]`

- `-c <chimera-run>` runs the engine leg: the script packages the core and
  plays a movie through Chimera's own engine, headless, with and without
  rerecording. Without `-c` the leg does not run.
- `-r <ROM dir>` runs the MT-32 legs with the Roland CM-32L ROMs from that
  folder (`CM32L_CONTROL.ROM` and `CM32L_PCM.ROM`, or `cm32l_ctrl_1_02.rom` and
  `cm32l_pcm.rom`). Without `-r` only the two MT-32 refusals run (no ROMs,
  files that are not ROMs), and the script says so. CI does not pass `-r`.
- `-g [<id>=]<file>` adds the equivalence, rerecord and session legs on a real
  release: once for each of the release's files, each under its firmware id
  (`-g dos-disk=<file>`). `-R <release>` names the release (`dos` when
  omitted). `-M <movie>` plays a movie of your own; without it the run is 2000
  steps from power-on. Without `-g` these legs do not run. CI does not pass
  `-g`: the game's files are the user's.
- `-f <frames>` sets the length of the synthetic runs (100 when omitted).

### Chimera's contract tests

Run after the package is in `<chimera>/build/Cores`:

```
cd <chimera>
CHIMERA_CORES_DIR=<chimera>/build/Cores dotnet test source/gui/Chimera.Tests.Client.Common/Chimera.Tests.Client.Common.csproj \
  -c Release --nologo \
  --filter "FullyQualifiedName~InstalledCorePackagesTests|FullyQualifiedName~MnemonicUniquenessTests"
```

They run Chimera's own checks against this package: it is readable, it is
built for an ABI this frontend runs, it makes a working factory, it binds only
buttons its controller declares, and it stamps a version. They need the
Chimera solution built and no game files.

## Files the core needs at run time

The package carries none of the game's data. A project picks a release (the
System in Chimera's new-project wizard) and brings that release's own files as
firmware. `waterbox/file_slots.json` declares no file slots.
`waterbox/waterbox.config` declares the firmware:

| Release (`release` setting) | Files the user provides (firmware id) |
| --- | --- |
| Another World (DOS) (`dos`) | `dos-disk`: the disk image (.img, .ima), or a zip of MEMLIST.BIN and the BANK files |
| Another World (Amiga, English) (`amiga`) | `amiga-en-disk1`, `amiga-en-disk2`: the two disks (.adf, or a zip holding one) |
| Another World (Amiga, French) (`amigafr`) | `amiga-fr-disk1`, `amiga-fr-disk2`: the two disks |
| Another World (Atari ST) (`atari`) | `atari-disk1`, `atari-disk2`: the two disks (.st, .msa, .stx, or a zip holding one) |
| Out of This World (Windows 3.1) (`win31`) | `BANK`, `WORLD.EXE`, `X.MID`, `Y.MID` |
| Out of This World (3DO) (`3do`) | `3do-disc`: the disc (.iso or .chd) |
| Another World 15th Anniversary Edition (`15th`) | `Pak01.pak`, `Intro2004.ogg`, `End2004.ogg`, and the texts of the chosen language: `lang_English.Txt`, `lang_Francais.Txt` or `lang_Espanol.txt` |
| Another World 20th Anniversary Edition (`20th`) | `20th-game`: a zip of its game folder |
| Out of This World (DOS demo) (`dosdemo`) | `dos-demo`: `ootwdemo.zip` as it is distributed |
| Another World (Atari ST rolling demo) (`stdemo`) | `atari-demo-disk`: the disk image (.st or .stx) |

Two more firmware entries are no files of the game's, and are asked for only
with their setting on:

- `CM32L_CONTROL.ROM` and `CM32L_PCM.ROM`: the Roland CM-32L's ROMs, for the
  MT-32 Sound Effects setting (`mt32`) on the DOS release and its demo.
- `soundfont.sf2`: a General MIDI SoundFont of the user's choice, for the
  SoundFont Music setting (`soundFont`) on the Windows 3.1 release.

The core does not open installers: an installer is refused, naming the files
to take out of it (`README.md`).

## Troubleshooting

- `miniBox not found at ...; pass -m <path> or set MINIBOX_DIR`
  (`build-package.sh`): no miniBox was named and none is at
  `~/chimera/extern/chimera-common-minibox`. Pass `-m`, `-r` or `MINIBOX_DIR`.
- `miniBox's C++ guest toolchain is missing: ...` (`guest.mk`): miniBox was
  built without `-Dguest_cpp=true`. Build `build/meson-cpp` as in "Build
  miniBox".
- `extern/rawgl is not checked out` (`apply-patches.sh`): the clone was made
  without its submodules. Run the `git submodule update` command it prints.
- `extern/rawgl is partly patched` (`apply-patches.sh`): the submodule's tree
  is neither pristine nor what the whole series leaves. The script prints the
  way back:
  `git -C extern/rawgl reset --hard && git -C extern/rawgl clean -fd && waterbox/apply-patches.sh`.
  That discards edits made in the tree: turn them into a patch first.
- `the series does not apply to the submodule's HEAD at <patch>`
  (`apply-patches.sh`): the submodule was moved without rebasing the patches.
- `the guest build failed (build/package-make.log)` (`build-package.sh`): read
  that log. The gate's build logs are `build/gate-native.log` and
  `build/gate-guest.log`.
- `git status` shows `extern/rawgl` as modified after a build: the patches are
  applied in the submodule's working tree. That is expected, and it does not
  mark the package `-dirty`.
- `(the MT-32 legs with ROMs need -r <ROM dir>)` in the gate's output is not a
  failure: those legs need ROMs the repository does not carry.
