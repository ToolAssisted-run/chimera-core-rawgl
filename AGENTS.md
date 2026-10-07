# AGENTS.md - rawgl core for Chimera

This repository builds rawgl, the reimplementation of the engine of Another
World (Out of This World), as a core for Chimera
(https://github.com/ToolAssisted-run/chimera), a frontend for tool-assisted
speedruns. It is a GAME core (`"kind": "game"`): one game engine built as a
core, not an emulator. It produces one file, `rawgl.chimeraCore`: the engine
built as a sandboxed guest (`core.wbx`) plus its declarations. The game's own
files are never in the repository or the package: the user provides them.

## Layout

- `extern/rawgl`: upstream rawgl, a submodule. Never edited in place.
- `extern/{miniz,zlib,stb,TinySoundFont,munt,libchdr}`: library submodules.
- `patches/`: the three numbered patches for `extern/rawgl`, applied by
  `waterbox/apply-patches.sh`.
- `waterbox/sources.mk`: the sources and defines both builds share.
- `waterbox/guest.mk`: builds `build/guest/core.wbx` (the sandboxed core).
- `waterbox/native.mk`: builds `build/native/run-native` and `run-wbx` (the
  native reference and the harness that runs `core.wbx`).
- `waterbox/build-package.sh`: builds the package. `run-gate.sh`: the gate.
- `waterbox/rawgl-driver.cpp`: the machine. `files.c`, `disks.c`: the
  project's files. `sdl-shim.cpp`, `compat/`: what stands in for SDL.
  `wbx-entry.c`: the guest's exports.
- `waterbox/waterbox.config`: what Chimera is told (the releases as machines,
  settings, firmware). `file_slots.json`: no slots. `default_keybinds.json`.
- `waterbox/package-licenses.json`: the licence terms the package carries.
- `waterbox/tests/`: the synthetic game (`make-synthetic.py`) and its movies.
- `.github/workflows/chimera.yml`: CI. It gates, packages and publishes.
- `build/`: every output. Ignored by git.

## Set up the build environment

Linux only (CI: `ubuntu-latest`). `<chimera>` is a Chimera checkout.

```
sudo apt-get update
sudo apt-get install -y --no-install-recommends meson ninja-build build-essential cmake pkg-config python3 mono-complete xvfb libgl1-mesa-dev libegl-dev libx11-dev libxext-dev libasound2-dev python3-pip

git submodule update --init --recursive
git clone --recursive https://github.com/ToolAssisted-run/chimera.git <chimera>

# Chimera itself: chimera-run (the gate's engine leg) and the contract tests.
# The dotnet line needs the .NET SDK 8.0.
cd <chimera>
meson setup build/meson-linux --prefix "$PWD/build" --libdir dll
meson compile -C build/meson-linux
meson install -C build/meson-linux
dotnet build source/gui/Chimera.sln -c Release /nodeReuse:false -p:UseSharedCompilation=false

# miniBox: the sandbox host and the C++ guest toolchain
mb=<chimera>/extern/chimera-common-minibox
[ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
meson compile -C "$mb/build/meson-linux"
[ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
meson compile -C "$mb/build/meson-cpp"
```

The scripts take miniBox from `-m <dir>` (or `MB=<dir>` for `make`), else
`MINIBOX_DIR`, else `~/chimera/extern/chimera-common-minibox`.

## Build

From the repository root. One command applies the patches, builds the guest
and writes the package:

```
./waterbox/build-package.sh -r <chimera>
```

`-r <chimera>` writes `<chimera>/build/Cores/rawgl.chimeraCore` and uses that
checkout's miniBox. `-o <dir>` writes `<dir>/rawgl.chimeraCore`. With neither
the package is `build/package/rawgl.chimeraCore`. `-m <dir>` names miniBox.

The native reference and the guest alone:

```
make -C waterbox -f native.mk MB=<chimera>/extern/chimera-common-minibox -j"$(nproc)"
make -C waterbox -f guest.mk MB=<chimera>/extern/chimera-common-minibox -j"$(nproc)"
```

A hand-built package stamps its version `<commit>+local` (`-dirty` when the
tree has changes) and is for testing. CI stamps the commit through
`CORE_VERSION` and publishes the releases.

## Install the core into Chimera

Chimera ships no cores and downloads nothing: a core is a file in its cores
folder. In a source checkout that is `<chimera>/build/Cores/`, where
`build-package.sh -r <chimera>` writes. In a release bundle it is the `Cores`
folder beside `Chimera.exe`, or the one chosen in File > Core Manager >
Change folder... File > Core Manager lists the folder; Refresh List rescans
it. The same package works on Linux and on Windows.

## Test before you commit

The gate must end with `gate: all legs passed`:

```
MINIBOX_DIR=<chimera>/extern/chimera-common-minibox ./waterbox/run-gate.sh -c <chimera>/build/dll/chimera-run
```

It builds both flavours itself and runs on a synthetic game it writes, so it
needs no game files. `-c` adds the engine leg (the package through
`chimera-run`). Legs on content the repository does not carry run only when
you name it: `-r <ROM dir>` (the MT-32 legs, with the Roland CM-32L ROMs),
and `-g <id>=<file>` with `-R <release>` and `-M <movie>` (a real release).

Then, with the package in `<chimera>/build/Cores`, Chimera's contract tests:

```
cd <chimera>
CHIMERA_CORES_DIR=<chimera>/build/Cores dotnet test source/gui/Chimera.Tests.Client.Common/Chimera.Tests.Client.Common.csproj \
  -c Release --nologo \
  --filter "FullyQualifiedName~InstalledCorePackagesTests|FullyQualifiedName~MnemonicUniquenessTests"
```

## Rules of this repository

- `extern/rawgl` is a submodule. A change to it is a numbered patch in
  `patches/`, applied by `waterbox/apply-patches.sh`. Never commit inside the
  submodule. The script takes the series as a whole: a pristine tree gets all
  of it, a fully patched tree is left alone, anything else is refused with the
  command to reset the tree. The other submodules carry no patches.
- Determinism is the product. The guest must not read host time, host
  randomness or anything else that differs between runs, and a savestate must
  round-trip. The gate checks it (equivalence, rerecord, session, clock). A
  change that breaks it is a bug.
- Both builds compile the same sources with the same defines
  (`waterbox/sources.mk`). Add a source or a define there, not in one makefile.
- Run the gate before committing. A new leg needs a negative control: show it
  fails when the thing it checks is broken (the gate's own `teeth` leg is the
  model; Chimera's `docs/gates.md` says why).
- Never commit game files, ROMs, SoundFonts or movies made on the real game.
  `tests/roms-local/` is ignored by git. Never add network access.
- The copy protection stays: the core is built without upstream's
  `BYPASS_PROTECTION`, and nothing of the core offers a way around it
  (`README.md`; `docs/PLAN.md`, Decisions).
- Shell scripts stay executable (git mode 100755): `waterbox/*.sh` and
  `waterbox/tests/make-synthetic.py`.
- Documentation prose is plain ASCII.
- Commit messages: the subject states what is now true, in the present tense,
  usually after a `rawgl:` or `ci:` prefix ("rawgl: the real Atari ST release
  runs"). The body says what changed and why, and what the gate showed.
- Do not edit `.github/workflows` unless the task is the workflow.

## Where to read more

- `docs/BUILDING.md`: every option, the files a user provides, troubleshooting.
- `README.md`: what the core is, the releases and their files, the patches.
- `docs/PLAN.md`: the decisions behind the core, and what is left.
- The header comments of the scripts and makefiles in `waterbox/`.
- Chimera's `docs/`: `game-cores.md`, `porting-a-core.md`, `core-manager.md`,
  `gates.md`.
