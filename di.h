/*
 * di is a dynamically typed value system capable of storing JSON values.
 * The values are immutable and are implemented using in-place update
 * optimizations when possible.
 *
 * The type di_t is a single type capable of storing any JSON value. A
 * di_t is 64 bits on both 32-bit and 64-bit platforms. Small values are
 * stored using NaN-boxing while larger values require memory allocations.
 *
 * Small values using NaN-boxing:
 *
 *   - doubles (64-bit)
 *   - integers (32-bit)
 *   - booleans and null
 *   - short strings (up to 6 bytes)
 *   - empty array (maybe TODO)
 *   - empty dict (maybe TODO)
 *
 * Large values using pointers to reference-counted allocated memory:
 *
 *   - aadeque for arrays
 *   - oaht for dicts
 *   - length-prefixed strings
 *
 * Memory handling scheme:
 *
 *   - Functions are responsible for the values passed to them as parameters. If
 *     the reference-counter of a value is zero, the function must either free
 *     its memory or return it. This is true for functions constructing a value
 *     from one or more values, such as di_array_concat(), but not for accessor
 *     functions such as di_array_length(). Whether a function is freeing its
 *     arguments should be documented for each function, but it is not yet so.
 */

#ifndef DI_H
#define DI_H

typedef struct di_tagged {
	char     tag;
	unsigned refc;
} di_tagged_t;

#define DI_STRING 0x5
#define DI_ARRAY  0x10
#define DI_DICT   0x20

#define NANBOX_POINTER_TYPE di_tagged_t*

// Defining the prefix for nanbox functions to "di_" instead of "nanbox_".
#define NANBOX_PREFIX di

// Assert replacement
#include "tressa.h"

#include "nanbox.h"
#include "nanbox_shortstring.h"

// Now, our type is di_t
typedef unsigned di_size_t;

/*-------------------------*
 * Functions to check type *
 *-------------------------*/

static inline bool di_is_string(const di_t v);
static inline bool di_is_array(di_t v);
static inline bool di_is_dict(di_t v);

// From nanbox.h we've already got:
//bool di_is_int(di_t v);
//bool di_is_double(di_t v);
//bool di_is_number(di_t v);
//bool di_is_boolean(di_t v);
//bool di_is_null(di_t v);

/*-------------*
 * Alloc debug *
 *-------------*/

#ifdef DI_ALLOC_DEBUG
	#include <stdlib.h>
	#include <stdio.h>
	static inline void * debug_alloc(size_t n) {
		void * p = malloc(n);
		printf("Allocating %lu bytes at %p.\n", (unsigned long)n, p);
		return p;
	}
	static inline void * debug_realloc(void * p, size_t n) {
		void * new_ptr = realloc(p, n);
		printf("Reallocating %lu bytes from %p to %p.\n", (unsigned long)n, p, new_ptr);
		return new_ptr;
	}
	static inline void debug_free(void * p) {
		printf("Freeing %p.\n", p);
		free(p);
	}

	#define DYNSTR_ALLOC(sz) debug_alloc(sz)
	#define DYNSTR_REALLOC(p, sz, oldsz) debug_realloc((p), (sz))
	#define DYNSTR_FREE(p, sz) debug_free(p)

	#define OAHT_ALLOC(sz) debug_alloc(sz)
	#define OAHT_REALLOC(p, sz, oldsz) debug_realloc((p), (sz))
	#define OAHT_FREE(p, sz) debug_free(p)

	#define AADEQUE_ALLOC(sz) debug_alloc(sz)
	#define AADEQUE_REALLOC(p, sz, oldsz) debug_realloc((p), (sz))
	#define AADEQUE_FREE(p, sz) debug_free(p)
#endif


/*---------*
 * General *
 *---------*/

void di_error(di_t message);

// Returns true if a == b, but also for identical strings, arrays, hashtables
static inline bool di_equal(di_t a, di_t b);

/*---------------------------------------------------------------------------*
 * Reference-counter functions. These are necessary to use properly for      *
 * pointer types. For immidiate values, they are optional (no-op).           *
 *---------------------------------------------------------------------------*/

// Increment reference-counter
static inline void di_incref(di_t a);

// Decrement reference-counter
static inline void di_decref(di_t a);

// Decrement reference-counter and free memory if it reaches zero.
static inline void di_decref_and_free(di_t a);

// Free if the reference-counter is zero
static inline void di_cleanup(di_t a);

/*------------------*
 * String functions *
 *------------------*/

// Returns a pointer to the first character in the string. Implemented as a
// macro, but the type would be this:
// static inline char *di_string_chars(di_t string);

// Returns the length in bytes of a string.
di_size_t di_string_length(di_t string);

// Returns an empty string
static inline di_t di_string_empty(void);

// Creates a string by copying length bytes from chars.
di_t di_string_from_chars(const char *chars, di_size_t length);

static inline di_t di_string_from_cstring(const char *chars);

// Appends length chars to s. Reuses the memory of s if its reference counter
// is zero.
di_t di_string_append_chars(di_t s, const char *chars, di_size_t length);

// Creates a new string consisting of concatenated copies of s1 and s2.
// Frees or reuses the memory of s1 and s2 if their ref-counters are zero.
di_t di_string_concat(di_t s1, di_t s2);

// Returns a copy of the substring of s of length length, starting at the
// zero-based byte index start. Reuses the memory of s if its ref-counter == 0.
di_t di_string_substr(di_t s, di_size_t start, di_size_t length);

/*---------------------------------------*
 * String functions, low level           *
 * (mutating or using undefined content) *
 *---------------------------------------*/

// Creates a string but does not initialize its contents
di_t di_string_create_presized(di_size_t length);

// Resizes a string. If the size is increased, undefined bytes are inserted at
// the end. Returns the new string. The old string is freed (or reused).
di_t di_string_resize(di_t string, di_size_t length);

/*-----------------*
 * Array functions
 *-----------------*/

// Creates an empty array
di_t di_array_empty(void);

// Returns the number of elements in an array
di_size_t di_array_length(di_t array);

/*
 * Array low-level (using undefined content)
 */
// Creates an array of 'length' elements with undefined contents
//di_t di_array_create(di_size_t length);
//di_t di_array_resize(di_t array, di_size_t length);

/*
 * Array high-level
 */
// The index must exist.
di_t di_array_get(di_t array, di_size_t index);

// The index must exist. Frees or reuses the memory of array if its ref-counter
// is zero.
di_t di_array_set(di_t array, di_size_t index, di_t value);

// Returns an array of length length, starting at start. The interval must be
// within valid indices of the array. Frees or reuses the memory of array if its
// ref-counter is zero.
di_t di_array_slice(di_t array, di_size_t start, di_size_t length);

// Concatenates two arrays. Returns the new array. Frees or reuses the memory of
// a1 and a2 if their ref-counters are zero.
di_t di_array_concat(di_t a1, di_t a2);

// Add an element at the end of an array. Points a to the new array to the new
// array. If the ref-counter of the array poited to by a is zero, its memory is
// reused or freed. Otherwise it is left unmodified and a is pointed to a new
// array.
void di_array_push(di_t * a, di_t v);

// Removes an element at the end of an array. Returns the removed element and
// points a to the new array. Points a to the new array to the new
// array. If the ref-counter of the array poited to by a is zero, its memory is
// reused or freed. Otherwise it is left unmodified and a is pointed to a new
// array.
di_t di_array_pop(di_t * a);

// Add an element at the beginning of an array. Points a to the new array to
// the new array. Points a to the new array to the new
// array. If the ref-counter of the array poited to by a is zero, its memory is
// reused or freed. Otherwise it is left unmodified and a is pointed to a new
// array.
void di_array_unshift(di_t * a, di_t v);

// Removes an element at the beginning of an array. Returns the removed element
// and points a to the new array. Points a to the new array to the new
// array. If the ref-counter of the array poited to by a is zero, its memory is
// reused or freed. Otherwise it is left unmodified and a is pointed to a new
// array.
di_t di_array_shift(di_t * a);

/*--------------------------------*
 * Dict (JSON "object") functions *
 *--------------------------------*/

// creates an empty dict
di_t di_dict_empty(void);

// Returns the number of entries in the dict
di_size_t di_dict_size(di_t dict);

// True if the key exists in the dict.
bool di_dict_contains(di_t dict, di_t key);

// Fetches a value from the dict. Null is returned if the key does not exist.
di_t di_dict_get(di_t dict, di_t key);

// Associates key with value. Returns the new dict. Frees or reuses the memory
// of dict if its reference counter is zero. If the key and/or value are already
// present in the dict, their memory is free'd if their ref-counters are zero.
di_t di_dict_set(di_t dict, di_t key, di_t value);

// Deletes the key if it exists. Returns the new dict. Frees or reuses the
// memory of dict if its reference counter is zero. Also frees key if the
// reference counter is zero.
di_t di_dict_delete(di_t dict, di_t key);

/*
 * A function to iterate over the keys and values. Start by passing pos = 0.
 * Pass the return value as pos to get the next entry. When 0 is returned,
 * there is no more entry to get.
 *
 * If a non-zero value is returned, key and value are assigned to point to a
 * key and a value in the dict.
 */
di_size_t di_dict_iter(di_t dict, di_size_t pos, di_t *key, di_t *value);

/*--------------------------------------------------------*
 * Definitions for static inline functions declared above *
 *--------------------------------------------------------*/

/*+-------------------------+*
 *| Functions to check type |*
 *+-------------------------+*/

// Immidiate types. From nanbox, we've got these directly:
//bool di_is_int(di_t v);
//bool di_is_double(di_t v);
//bool di_is_number(di_t v);
//bool di_is_boolean(di_t v);
//bool di_is_null(di_t v);

// Pointer types
static inline bool di_is_string(const di_t v)  {
	if (di_is_shortstring(v))
		return true;
	return di_is_pointer(v) &&
	       di_to_pointer(v)->tag == DI_STRING;
}
static inline bool di_is_array(di_t v) {
	return di_is_pointer(v) &&
	       di_to_pointer(v)->tag == DI_ARRAY;
}
static inline bool di_is_dict(di_t v) {
	return di_is_pointer(v) &&
	       di_to_pointer(v)->tag == DI_DICT;
}

/*---------*
 * General *
 *---------*/

bool di_ptr_equal(di_t v1, di_t v2);

// Returns true if v1 == v2, but also for strings, arrays and hashtables
// with identical contents
static inline bool di_equal(di_t v1, di_t v2) {
	if (!di_is_pointer(v1))
		return di_raw_value(v1) == di_raw_value(v2); // TODO: && !di_is_nan(v1);
	if (!di_is_pointer(v2))
		return false;
	if (di_raw_value(v1) == di_raw_value(v2))
		return true;
	return di_ptr_equal(v1, v2);
}

/*--------*
 * String *
 *--------*/

// using dynstr for > 6 bytes long strings
#define DYNSTR_HEADER di_tagged_t header;
#define DYNSTR_SIZE_T di_size_t
#include "dynstr.h"

// This must be a macro, to be able to return a pointer into its own argument.
#define di_string_chars(string) \
	(di_is_shortstring(string) ? di_shortstring_chars(&(string)) \
	                           : dynstr_chars((dynstr_t *)di_to_pointer(string)))

// Returns an empty string
static inline di_t di_string_empty(void) {
	return di_shortstring_create_undef(0);
}

static inline di_t di_string_from_cstring(const char *chars) {
	return di_string_from_chars(chars, strlen(chars));
}

/*--------------------*
 * Reference-counters *
 *--------------------*/

// Init tag and refc for any tagged type (used internally)
static inline void di_init_tagged(di_tagged_t *tagged, char tag) {
	tagged->tag  = tag;
	tagged->refc = 0;
}

// Increment reference-counter
static inline void di_incref(di_t v) {
	if (di_is_pointer(v))
		di_to_pointer(v)->refc++;
}

// Decrement reference-counter
static inline void di_decref(di_t v) {
	if (di_is_pointer(v))
		di_to_pointer(v)->refc--;
}

// Decrement reference-counter and free memory if it reaches zero.
static inline void di_decref_and_free(di_t v) {
	if (di_is_pointer(v)) {
		di_to_pointer(v)->refc--;
		di_cleanup(v);
	}
}

// Non-inline helper used by di_cleanup().
void di_ptr_free(di_t v);

// Free if the reference-counter is zero
static inline void di_cleanup(di_t v) {
	if (di_is_pointer(v) && di_to_pointer(v)->refc == 0)
		di_ptr_free(v);
}

#endif
