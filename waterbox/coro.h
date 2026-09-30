/* coro.h - coroutines for the core: rawgl's engine runs on a stack of its own
 * and hands control back where the program would wait (coro.c). */
#ifndef RAWGL_CORO_H
#define RAWGL_CORO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct coro coro;

/* a coroutine that will run fn on a stack of stack_size bytes; NULL if no memory */
coro *coro_create(void (*fn)(void), size_t stack_size);
/* runs the coroutine until it yields (or, the first time, starts it) */
void coro_resume(coro *c);
/* from inside the coroutine: back to whoever resumed it */
void coro_yield(coro *c);
void coro_destroy(coro *c);

#ifdef __cplusplus
}
#endif

#endif
