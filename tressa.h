/*
 * Tressa is assert backwards. It's a replacement for assert.h.
 */
#undef assert
#undef __assert


#ifdef NDEBUG
#define	assert(e)	((void)0)
#else

#ifndef TRESSA_DEFINED
/* define the inline function that we use for trace stuff */
#define TRESSA_DEFINED
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
static inline int tressa(const char *e, const char *file, int line) {
	void* callstack[128];
	int i, frames = backtrace(callstack, 128);
	char** strs = backtrace_symbols(callstack, frames);
	printf ("%s:%u: failed assertion `%s'\n", file, line, e);
	for (i = 1; i < frames; ++i) {
		printf("%s\n", strs[i]);
	}
	free(strs);
	exit(1);
	return 0;
}
#endif

#define assert(e)  \
    ((void) ((e) ? 0 : tressa (#e, __FILE__, __LINE__)))


#endif
