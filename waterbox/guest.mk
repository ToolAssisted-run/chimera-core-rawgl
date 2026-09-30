# guest.mk - core.wbx: the same rawgl, miniz and core sources as native.mk,
# built with miniBox's musl/libstdc++ guest toolchain (rawgl is C++) and linked
# at the guest base. Objects land in build/guest; core.wbx is checked by
# miniBox's check-wbx.sh (no thread-local storage, no %fs, no red zone) before
# it counts as built.
#
# Needs miniBox built WITH the C++ guest toolchain:
#   meson setup <miniBox>/build/meson-cpp <miniBox> -Dguest_cpp=true && ninja -C <miniBox>/build/meson-cpp
#
# Usage: make -f guest.mk -j$(nproc) [MB=<miniBox checkout>]

.DEFAULT_GOAL := all
include sources.mk

B      := $(ROOT)/build/guest
MBUILD := $(MB)/build/meson-cpp
SR     := $(MBUILD)/guest-sysroot
GCCVER := $(shell gcc -dumpfullversion)
# the system's compilers over the guest sysroot, through its specs (musl's
# headers and start files, -mno-red-zone), as the DOSBox-X core's cross file
CC     := gcc -specs $(SR)/lib/musl-gcc.specs
CXX    := g++ -specs $(SR)/lib/musl-gcc.specs

# BizHawk waterbox's frozen guest flags, as miniBox's source/guest/meson.build
# gives them to a guest
WBFLAGS := -fvisibility=hidden -mcmodel=large -mno-red-zone -mstack-protector-guard=global \
	-fno-stack-protector -fno-pic -fno-pie -fcf-protection=none -DNDEBUG -DCHIMERA_GUEST
MBINCS := -I$(MB)/extern/emulibc -I$(MB)/source/guest/include -I$(MB)/extern/jsmn
CXXINCS := -I$(SR)/include/c++/$(GCCVER) -I$(SR)/include/c++/$(GCCVER)/x86_64-linux-musl

RAWGL_CXXFLAGS := $(WBFLAGS) $(RAWGL_CXXFLAGS_COMMON) $(CXXINCS) -w
MINIZ_CFLAGS := $(WBFLAGS) $(MINIZ_CFLAGS_COMMON) -w
ZLIB_CFLAGS := $(WBFLAGS) $(ZLIB_CFLAGS_COMMON) -w
CORE_CFLAGS := $(WBFLAGS) $(CORE_CFLAGS_COMMON) $(MBINCS) -I. -Wall -Wno-unused-function
CORE_CXXFLAGS := $(WBFLAGS) $(CORE_CXXFLAGS_COMMON) $(CXXINCS) $(MBINCS) -I. -Wall -Wno-unused-function

$(call flags_stamp,$(B),$(CC) | $(CXX) | $(RAWGL_CXXFLAGS) | $(MINIZ_CFLAGS) | $(ZLIB_CFLAGS) | $(CORE_CFLAGS) | $(CORE_CXXFLAGS))

RAWGL_OBJS := $(patsubst $(RAWGL)/%.cpp,$(B)/rawgl/%.o,$(RAWGL_SRCS))
MINIZ_OBJS := $(patsubst $(MINIZ)/%.c,$(B)/miniz/%.o,$(MINIZ_SRCS))
ZLIB_OBJS := $(patsubst $(ZLIB)/%.c,$(B)/zlib/%.o,$(ZLIB_SRCS))
CORE_OBJS := $(addprefix $(B)/core/,$(addsuffix .o,$(CORE_C_NAMES) $(CORE_CXX_NAMES)))

all: $(B)/core.wbx

$(SR)/lib/libstdc++.a:
	@echo "miniBox's C++ guest toolchain is missing: $(SR)" >&2
	@echo "build it: meson setup $(MBUILD) $(MB) -Dguest_cpp=true && ninja -C $(MBUILD)" >&2
	@false

$(B)/rawgl/%.o: $(RAWGL)/%.cpp $(wildcard compat/*.h) $(PATCH_STAMP) $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CXX) $(RAWGL_CXXFLAGS) -c -o $@ $<

$(B)/miniz/%.o: $(MINIZ)/%.c $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CC) $(MINIZ_CFLAGS) -c -o $@ $<

$(B)/zlib/%.o: $(ZLIB)/%.c $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CC) $(ZLIB_CFLAGS) -c -o $@ $<

$(B)/core/%.o: %.c $(CORE_HDRS) $(PATCH_STAMP) $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CC) $(CORE_CFLAGS) -c -o $@ $<

$(B)/core/%.o: %.cpp $(CORE_HDRS) $(PATCH_STAMP) $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CXX) $(CORE_CXXFLAGS) -c -o $@ $<

# the guest kit's link recipe (the DOSBox-X core's): the large code model's
# --no-relax, the weak pthread pulls libgcc_eh needs, cxxglue for the unwinder
$(B)/core.wbx: $(CORE_OBJS) $(RAWGL_OBJS) $(MINIZ_OBJS) $(ZLIB_OBJS)
	$(CXX) -static -no-pie -Wl,--eh-frame-hdr -Wl,-O2 -Wl,--no-relax -Wl,-z,stack-size=8388608 \
		-T $(MB)/source/guest/linkscript.T \
		-Wl,-u,pthread_once -Wl,-u,pthread_cond_wait -Wl,-u,pthread_cond_broadcast -Wl,-u,pthread_key_create \
		-o $@.tmp $^ $(MBUILD)/source/guest/cxxglue.c.o $(MBUILD)/source/guest/emulibc.c.o $(WRAP_FLAGS) \
		-L$(SR)/lib -lstdc++ -lm -lgcc -lgcc_eh -lc
	sh $(MB)/source/guest/check-wbx.sh $@.tmp
	mv $@.tmp $@

clean:
	rm -rf $(B)

.PHONY: all clean
