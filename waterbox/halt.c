/* halt.c - a failed assertion in rawgl halts the machine, as its error() does.
 *
 * Upstream builds with its assertions on and a failed one aborts the process.
 * The core keeps them on in both builds (sources.mk) and answers the C
 * library's __assert_fail itself - musl's in the guest, glibc's natively (an
 * executable's definition comes first) - so a check the game's data fails
 * stops the machine where it stands, with the check's text as the reason,
 * instead of taking the frontend down. The engine's code runs on its own
 * stack (rawgl-driver.cpp), which is the only place an assertion of rawgl's
 * can fire; halting there parks that stack for good.
 *
 * No <assert.h> here: musl and glibc disagree on the line's type (int,
 * unsigned int), and neither prototype is needed to define the function.
 */
#include <stdio.h>

void rawgl_error_hook(const char *msg);

_Noreturn void __assert_fail(const char *expr, const char *file, int line, const char *func)
{
	char msg[512];
	snprintf(msg, sizeof msg, "%s:%d: %s: assertion failed: %s", file, line, func, expr);
	fprintf(stderr, "ERROR: %s!\n", msg);
	rawgl_error_hook(msg);
	for (;;)
		;
}
