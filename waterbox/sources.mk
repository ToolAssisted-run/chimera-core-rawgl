# sources.mk - what native.mk and guest.mk both build: the same files with the
# same defines, so the native reference and the sandboxed core are the same
# program. Included, not run.

ROOT := ..
RAWGL := $(ROOT)/extern/rawgl
MINIZ := $(ROOT)/extern/miniz
MB    ?= $(or $(MINIBOX_DIR),$(HOME)/chimera/extern/chimera-common-minibox)

# ---- rawgl, upstream: its Makefile's SRCS less its frontend - main.cpp,
# systemstub_sdl.cpp and graphics_gl.cpp (the SDL window and the OpenGL
# renderer); the core is the frontend instead. mixer.cpp is compiled as it is,
# against the core's SDL_mixer device (compat/, sdl-shim.cpp).
#
# Upstream's Makefile defines BYPASS_PROTECTION; the core does not: the game
# starts where the original starts, at the copy protection's symbols (DOS with
# a password screen, Amiga, Atari ST), as rawgl's own code has it without the
# define. RAWGL_MEMFS is the core's (patches/0001): files come from memory.
RAWGL_NAMES := aifcplayer bitmap file engine graphics_soft script mixer pak resource resource_nth \
	resource_win31 resource_3do scaler screenshot sfxplayer staticres unpack util video
RAWGL_SRCS := $(addprefix $(RAWGL)/,$(addsuffix .cpp,$(RAWGL_NAMES)))
# as upstream's Makefile, less -g, SDL and its defines: -O2 for a release.
# Its assertions stay on, in both builds (-UNDEBUG undoes the guest flags'
# -DNDEBUG): upstream's checks on the data are part of the program, and a
# failed one halts the machine with its message (halt.c) where upstream's
# process would abort.
RAWGL_CXXFLAGS_COMMON := -std=gnu++11 -O2 -UNDEBUG -DRAWGL_MEMFS -Icompat -I$(RAWGL) -I$(MINIZ) -DMINIZ_NO_STDIO -DMINIZ_NO_TIME

# ---- miniz (the submodule extern/miniz, tag 3.0.2): the zip reader the
# project's game files come through, and the zlib the engine's 15th/20th
# Anniversary readers link against (the core refuses those releases, but
# resource_nth.cpp is built as upstream builds it)
MINIZ_NAMES := miniz miniz_tdef miniz_tinfl miniz_zip
MINIZ_SRCS := $(addprefix $(MINIZ)/,$(addsuffix .c,$(MINIZ_NAMES)))
# miniz runs at Init, off the engine's stack: its assertions are off in both
# builds (the guest flags' -DNDEBUG, and here the native build's)
MINIZ_CFLAGS_COMMON := -std=gnu11 -O2 -DNDEBUG -Icompat -I$(MINIZ) -DMINIZ_NO_STDIO -DMINIZ_NO_TIME

# ---- the core
CORE_C_NAMES := coro files halt wbx-entry
CORE_CXX_NAMES := rawgl-driver sdl-shim
CORE_HDRS := rawgl-driver.h rawgl-files.h rawgl-audio.h coro.h $(wildcard compat/*.h) $(wildcard $(RAWGL)/*.h)
CORE_CFLAGS_COMMON := -std=gnu11 -O2 -Icompat -I$(MINIZ) -DMINIZ_NO_STDIO -DMINIZ_NO_TIME
CORE_CXXFLAGS_COMMON := $(RAWGL_CXXFLAGS_COMMON)

# the calls the core answers itself: the engine's clock at start (the random
# seed) is the project's setting (rawgl-driver.cpp)
WRAP_FLAGS := -Wl,--wrap=time

# the patch series goes onto the submodule before anything of rawgl builds
PATCH_STAMP := $(ROOT)/build/patches.stamp
$(PATCH_STAMP): $(wildcard $(ROOT)/patches/*.patch) apply-patches.sh
	sh apply-patches.sh
	@mkdir -p $(dir $@)
	@touch $@

# Every object depends on the flags it was built with: a change to them
# rebuilds it.
define flags_stamp
$(shell mkdir -p $(1); printf '%s\n' '$(2)' | cmp -s - $(1)/flags || printf '%s\n' '$(2)' > $(1)/flags)
endef
