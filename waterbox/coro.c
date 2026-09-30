/* coro.c - coroutines for the core (coro.h), over a stack asked for with mmap.
 *
 * rawgl's engine keeps the original program's shape - a loop that sleeps
 * between frames, a pause that waits for a key - and runs on a stack of its
 * own, handing control back where the program would wait. Nothing is threaded:
 * a switch saves exactly what the System V ABI says a callee must preserve
 * (rbx, rbp, r12-r15, the MXCSR and the x87 control word) on the stack being
 * left, and stores that stack pointer. Everything a suspended coroutine holds
 * is on its own stack, which is guest memory like any other, so a savestate
 * taken between two steps carries it whole. (From chimera-core-sdlpop2.)
 *
 * The stacks are asked for with MAP_STACK: miniBox tracks writes by protecting
 * pages, and on Windows a fault on the page the stack pointer is in cannot be
 * delivered unless the sandbox was told the page is a stack (miniBox
 * memblock.c; the x68k and ares cores both died on Windows without it).
 * Natively the flag costs nothing.
 */
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "coro.h"

#ifndef MAP_STACK
#define MAP_STACK 0x20000
#endif

struct coro
{
	void *sp;         /* where the coroutine resumes */
	void *caller_sp;  /* where its resumer resumes */
	void (*fn)(void);
	void *base;
	size_t size;
};

__asm__(
	".text\n"
	".globl rawgl_co_switch\n"
	".type rawgl_co_switch,@function\n"
	"rawgl_co_switch:\n"            /* void rawgl_co_switch(void **save_sp, void *load_sp) */
	"	pushq %rbp\n"
	"	pushq %rbx\n"
	"	pushq %r12\n"
	"	pushq %r13\n"
	"	pushq %r14\n"
	"	pushq %r15\n"
	"	subq $8, %rsp\n"
	"	stmxcsr (%rsp)\n"
	"	fnstcw 4(%rsp)\n"
	"	movq %rsp, (%rdi)\n"
	"	movq %rsi, %rsp\n"
	"	ldmxcsr (%rsp)\n"
	"	fldcw 4(%rsp)\n"
	"	addq $8, %rsp\n"
	"	popq %r15\n"
	"	popq %r14\n"
	"	popq %r13\n"
	"	popq %r12\n"
	"	popq %rbx\n"
	"	popq %rbp\n"
	"	ret\n"
	".size rawgl_co_switch, .-rawgl_co_switch\n"
	"\n"
	".globl rawgl_co_boot\n"
	".type rawgl_co_boot,@function\n"
	"rawgl_co_boot:\n"              /* the first switch into a new stack returns here */
	"	andq $-16, %rsp\n"
	"	call rawgl_co_entry\n"
	"	ud2\n"
	".size rawgl_co_boot, .-rawgl_co_boot\n");

void rawgl_co_switch(void **save_sp, void *load_sp);
void rawgl_co_boot(void);

/* The coroutine being started: the first switch into a stack carries no
 * argument, so the one about to run is said here (as source/coro.c does). */
static coro *starting;

void rawgl_co_entry(void)
{
	coro *c = starting;
	c->fn();
	for (;;) coro_yield(c);   /* a finished coroutine yields for ever */
}

coro *coro_create(void (*fn)(void), size_t stack_size)
{
	coro *c = calloc(1, sizeof *c);
	if (!c) return NULL;
	size_t size = (stack_size + 4095) & ~(size_t)4095;
	void *base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
	if (base == MAP_FAILED)
	{
		free(c);
		return NULL;
	}
	c->fn = fn;
	c->base = base;
	c->size = size;

	/* A frame rawgl_co_switch can "return" through: the saved control words,
	 * six zeroed callee-saved registers, and rawgl_co_boot as the return
	 * address, with one slot of padding above it so that boot starts with the
	 * stack as a call would have left it. */
	uint64_t *sp = (uint64_t *)((uint8_t *)base + size);
	*--sp = 0;
	*--sp = (uint64_t)(uintptr_t)rawgl_co_boot;
	for (int i = 0; i < 6; i++) *--sp = 0;
	uint32_t mxcsr = __builtin_ia32_stmxcsr();
	uint16_t fpucw;
	__asm__ volatile("fnstcw %0" : "=m"(fpucw));
	*--sp = (uint64_t)mxcsr | ((uint64_t)fpucw << 32);
	c->sp = sp;
	return c;
}

void coro_resume(coro *c)
{
	starting = c;
	rawgl_co_switch(&c->caller_sp, c->sp);
}

void coro_yield(coro *c) { rawgl_co_switch(&c->sp, c->caller_sp); }

void coro_destroy(coro *c)
{
	if (!c) return;
	munmap(c->base, c->size);
	free(c);
}
