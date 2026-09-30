/* munt-config.h - Munt's config.h (mt32emu/src/config.h.in) for the core:
 * libmt32emu 2.8.3 as the submodule extern/munt has it, built into the core
 * statically, every API type available, no version tagging. sources.mk puts a
 * copy named config.h where Munt's #include "config.h" finds it. */
#ifndef MT32EMU_CONFIG_H
#define MT32EMU_CONFIG_H

#define MT32EMU_VERSION      "2.8.3"
#define MT32EMU_VERSION_MAJOR 2
#define MT32EMU_VERSION_MINOR 8
#define MT32EMU_VERSION_PATCH 3

#define MT32EMU_EXPORTS_TYPE 3

#define MT32EMU_WITH_VERSION_TAGGING 0
#undef MT32EMU_RUNTIME_VERSION_CHECK

#endif
