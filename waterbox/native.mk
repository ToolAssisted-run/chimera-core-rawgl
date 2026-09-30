# native.mk - the native reference: the same rawgl, miniz and core sources as
# guest.mk, built for the host, plus the two harnesses (run-native drives the
# exports directly; run-wbx drives core.wbx through the miniBox host exactly as
# the frontend does). Objects land in build/native.
#
# Usage: make -f native.mk -j$(nproc) [MB=<miniBox checkout>]

.DEFAULT_GOAL := all
include sources.mk

B := $(ROOT)/build/native
MBINCS := -Inative-shim -I$(MB)/source/guest/include -I$(MB)/extern/jsmn

RAWGL_CXXFLAGS := $(RAWGL_CXXFLAGS_COMMON) -w
MINIZ_CFLAGS := $(MINIZ_CFLAGS_COMMON) -w
ZLIB_CFLAGS := $(ZLIB_CFLAGS_COMMON) -w
CORE_CFLAGS := $(CORE_CFLAGS_COMMON) $(MBINCS) -I. -Wall -Wno-unused-function
CORE_CXXFLAGS := $(CORE_CXXFLAGS_COMMON) $(MBINCS) -I. -Wall -Wno-unused-function

$(call flags_stamp,$(B),$(RAWGL_CXXFLAGS) | $(MINIZ_CFLAGS) | $(ZLIB_CFLAGS) | $(CORE_CFLAGS) | $(CORE_CXXFLAGS))

RAWGL_OBJS := $(patsubst $(RAWGL)/%.cpp,$(B)/rawgl/%.o,$(RAWGL_SRCS))
MINIZ_OBJS := $(patsubst $(MINIZ)/%.c,$(B)/miniz/%.o,$(MINIZ_SRCS))
ZLIB_OBJS := $(patsubst $(ZLIB)/%.c,$(B)/zlib/%.o,$(ZLIB_SRCS))
CORE_OBJS := $(addprefix $(B)/core/,$(addsuffix .o,$(CORE_C_NAMES) $(CORE_CXX_NAMES)))

all: $(B)/run-native $(B)/run-wbx

$(B)/rawgl/%.o: $(RAWGL)/%.cpp $(wildcard compat/*.h) $(PATCH_STAMP) $(B)/flags
	@mkdir -p $(dir $@)
	g++ $(RAWGL_CXXFLAGS) -c -o $@ $<

$(B)/miniz/%.o: $(MINIZ)/%.c $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(MINIZ_CFLAGS) -c -o $@ $<

$(B)/zlib/%.o: $(ZLIB)/%.c $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(ZLIB_CFLAGS) -c -o $@ $<

$(B)/core/%.o: %.c $(CORE_HDRS) $(PATCH_STAMP) $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(CORE_CFLAGS) -c -o $@ $<

$(B)/core/%.o: %.cpp $(CORE_HDRS) $(PATCH_STAMP) $(B)/flags
	@mkdir -p $(dir $@)
	g++ $(CORE_CXXFLAGS) -c -o $@ $<

$(B)/core/run-native.o: run-native.c gate-harness.h rawgl-driver.h $(B)/flags
	@mkdir -p $(dir $@)
	gcc -O2 -Wall -DGATE_NATIVE -I. -c -o $@ $<

$(B)/run-native: $(CORE_OBJS) $(RAWGL_OBJS) $(MINIZ_OBJS) $(ZLIB_OBJS) $(B)/core/run-native.o
	g++ -o $@ $^ $(WRAP_FLAGS) -lm

# run-wbx links the miniBox host library
MBHOST := $(MB)/build/meson-linux/source/host
$(B)/run-wbx: run-wbx.c gate-harness.h rawgl-driver.h $(B)/flags
	gcc -O2 -Wall -I. -I$(MB)/source/host -o $@ run-wbx.c $(MBHOST)/libminiboxhost.so -Wl,-rpath,$(MBHOST)

clean:
	rm -rf $(B)

.PHONY: all clean
