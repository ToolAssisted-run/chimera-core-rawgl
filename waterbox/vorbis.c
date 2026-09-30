/* vorbis.c - Ogg Vorbis music for the 20th Anniversary Edition, with
 * stb_vorbis (extern/stb), decoding from memory. Its sin, cos, exp, log and
 * pow are detmath.h's, so both builds decode the same samples. */
#include <math.h>

#include "detmath.h"

#define sin(x) dm_sin(x)
#define cos(x) dm_cos(x)
#define exp(x) dm_exp(x)
#define log(x) dm_log(x)
#define pow(x, y) dm_pow(x, y)

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
/* upstream's code, as it is: its warnings are its own */
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include "stb_vorbis.c"
