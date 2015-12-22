#ifndef DYNSTR_H
#define DYNSTR_H
/*
 *------------------------
 * Dynamic strings
 *------------------------
 */

/*
 * Allocation macros
 *
 * Defaults to malloc, realloc and free.  Define them to your own wrappers to
 * monitor memory usage, etc.
 */
#ifndef DYNSTR_ALLOC
	#define DYNSTR_ALLOC(size) malloc(size)
#endif
#ifndef DYNSTR_REALLOC
	#define DYNSTR_REALLOC(ptr, size, oldsize) realloc(ptr, size)
#endif
#ifndef DYNSTR_FREE
	#define DYNSTR_FREE(ptr, size) free(ptr)
#endif
#ifndef DYNSTR_OOM
	#define DYNSTR_OOM() exit(-1)
#endif

/* type for string lengths and string indices */
#ifndef DYNSTR_SIZE_T
	#define DYNSTR_SIZE_T size_t
#endif

#include <stdlib.h>
#include <string.h>

struct dynstr {
	#ifdef DYNSTR_HEADER
	DYNSTR_HEADER
	#endif
	DYNSTR_SIZE_T len;
	DYNSTR_SIZE_T cap;
	char chars[1];
};

typedef struct dynstr dynstr_t;

// Returns the length of a string.
static inline DYNSTR_SIZE_T dynstr_length(dynstr_t * const s) {
	return s->len;
}

// Returns a pointer to the characters in a string.
static inline char * dynstr_chars(dynstr_t * const s) {
	return s->chars;
}

// Frees the memory.
static inline void dynstr_destroy(dynstr_t * s) {
	DYNSTR_FREE(s, dynstr_sizeof(s->cap));
}

// Memory size of a string with capacity cap. Used internally.
static inline DYNSTR_SIZE_T dynstr_sizeof(DYNSTR_SIZE_T const cap) {
	return (DYNSTR_SIZE_T)sizeof(dynstr_t) + cap + 1;
}

// Creates a string with an initial capacity.
static inline dynstr_t * dynstr_create(DYNSTR_SIZE_T const capacity) {
	dynstr_t * s = (dynstr_t *)DYNSTR_ALLOC(dynstr_sizeof(capacity));
	s->cap = capacity;
	s->len = 0;
	s->chars[0] = '\0';
	return s;
}

// Reserve space for at least n more bytes. Used internally.
static inline dynstr_t * dynstr_reserve(dynstr_t * s, DYNSTR_SIZE_T const n) {
	DYNSTR_SIZE_T min_cap = s->len + n;
	if (s->cap < min_cap) {
		// realloc
		DYNSTR_SIZE_T new_cap = s->cap;
		while (new_cap < min_cap)
			new_cap *= 2;
		s = (dynstr_t *)DYNSTR_REALLOC(s, dynstr_sizeof(new_cap),
		                               dynstr_sizeof(s->cap));
		s->cap = new_cap;
	}
	return s;
}

// Frees some unused memory.
static inline dynstr_t * dynstr_compact(dynstr_t * s) {
	if (s->cap > s->len) {
		s = (dynstr_t *)DYNSTR_REALLOC(s, dynstr_sizeof(s->len),
		                               dynstr_sizeof(s->cap));
		s->cap = s->len;
	}
	return s;
}

// Create a dynstr by copying length bytes from chars
static inline dynstr_t *
dynstr_from_chars(char const * const chars, DYNSTR_SIZE_T const length) {
	dynstr_t * s = (dynstr_t *)DYNSTR_ALLOC(dynstr_sizeof(length));
	s->cap = s->len = length;
	memcpy(s->chars, chars, length);
	s->chars[s->len] = '\0';
	return s;
}

// Appends length chars to s. Reallocates s if necessary.
static inline dynstr_t *
dynstr_append_chars(dynstr_t *s, char const * const chars,
                    DYNSTR_SIZE_T const length) {
	s = dynstr_reserve(s, length);
	memcpy(&s->chars[s->len], chars, length);
	s->len += length;
	s->chars[s->len] = '\0';
	return s;
}

#endif
