#include "di.h"
#include "di_debug.h"
#include <stdio.h>

/*------------------------------------------*
 * Dummy error handling: DIE(message) macro *
 *------------------------------------------*/
#define DIE(msg) do { \
	fprintf(stderr, \
	       "Fatal error: %s on line %d in %s\n", \
	       msg, __LINE__, __FILE__); \
	exit(-1); \
} while(0)

/*-----------------------------------------*
 * Official error handling                 *
 *-----------------------------------------*/

// Raises an error. Doesn't return.
void di_error(di_t message) {
	if (di_is_string(message)) {
		fprintf(stderr, "Error: %.*s\n", di_string_length(message),
		        di_string_chars(message));
	} else {
		fprintf(stderr, "Error (non-string message)\n");
	}
	exit(1);
}

/*+--------+*
 *| String |*
 *+--------+*/

// Returns the length in bytes of a string.
di_size_t di_string_length(di_t v) {
	assert(di_is_string(v));
	if (di_is_shortstring(v))
		return di_shortstring_length(v);
	else
		return dynstr_length((dynstr_t *)di_to_pointer(v));
}

// Creates a string of length length containing undefined bytes.
// (Low-level, for interal use)
di_t di_string_create_presized(di_size_t length) {
	if (length <= 6)
		return di_shortstring_create_undef(length);
	dynstr_t * s = dynstr_create(length);
	s->len = length;
	s->chars[length] = '\0'; // dynstr are nul terminated
	di_tagged_t * tagged = (di_tagged_t *)s;
	di_init_tagged(tagged, DI_STRING);
	return di_from_pointer(tagged);
}

// Resizes a string. If the size is increased, undefined bytes are inserted at
// the end. Returns the new string. The old string is freed. Note: Must not be
// used if the string has refc > 0.
// (Low-level, for interal use)
di_t di_string_resize(di_t s, di_size_t length) {
	assert(di_is_string(s));
	assert(!di_is_pointer(s) || di_to_pointer(s)->refc == 0);
	di_size_t old_length = di_string_length(s);
	if (old_length == length) {
		return s; // No resize is necessary.
	}
	if (di_is_shortstring(s) || length <= 6) {
		// It is a shortstring or it will be a shortstring.
		// Create a new string and copy the chars to it
		di_t s2 = di_string_create_presized(length);
		di_size_t min_length =
			length < old_length ? length : old_length;
		memcpy(di_string_chars(s2), di_string_chars(s), min_length);
		di_cleanup(s);
		return s2;
	} else {
		// The string is a dynstr and we need a dynstr. Resize it.
		dynstr_t * dynstr = (dynstr_t *)di_to_pointer(s);
		if (old_length < length) {
			dynstr = dynstr_reserve(dynstr, length - old_length);
			dynstr->len = length;
		} else {
			dynstr->len = length;
			dynstr = dynstr_compact(dynstr);
		}
		return di_from_pointer((di_tagged_t *)dynstr);
	}
}

// Creates a string by copying length bytes from chars.
di_t di_string_from_chars(const char *chars, di_size_t length) {
	di_t s = di_string_create_presized(length);
	memcpy(di_string_chars(s), chars, length);
	return s;
}

// Appends length chars to s.
di_t di_string_append_chars(di_t s, const char *chars, di_size_t length) {
	assert(di_is_string(s));
	di_size_t old_length = di_string_length(s);
	di_t s2;
	if (di_is_shortstring(s) || di_to_pointer(s)->refc == 0) {
		// Resize and reuse the string
		s2 = di_string_resize(s, old_length + length);
	}
	else {
		// Create a new string and copy all chars to it
		s2 = di_string_create_presized(old_length + length);
		memcpy(di_string_chars(s2), di_string_chars(s), old_length);
	}
	memcpy(&di_string_chars(s2)[old_length], chars, length);
	return s2;
}

// Creates a new string consisting of concatenated copies of s1 and s2.
di_t di_string_concat(di_t s1, di_t s2) {
	di_t s = di_string_append_chars(s1, di_string_chars(s2),
	                                di_string_length(s2));
    di_cleanup(s2);
    return s;
}

// Returns a copy of the substring of s of length length, starting at the
// zero-based byte index start.
di_t di_string_substr(di_t s, di_size_t start, di_size_t length) {
	assert(di_is_string(s));
	assert(start >= 0 && length >= 0);
	assert(start + length <= di_string_length(s));
	if (start == 0 && length == di_string_length(s))
		return s; // The whole string
	if (di_is_pointer(s) && di_to_pointer(s)->refc == 0) {
		// reuse the string, move chars to the beginning and shrink
		if (start != 0) {
			// Move the chars to the beginning
			char * chars = di_string_chars(s);
			memmove(chars, &chars[start], length);
		}
		return di_string_resize(s, length);
	}
	// Otherwise, copy the chars to a new string
	return di_string_from_chars(&di_string_chars(s)[start], length);
}

/*+-------+*
 *| Array |*
 *+-------+*/

/* Use aadeque_t for the array implementation */
#define AADEQUE_HEADER di_tagged_t header;
#define AADEQUE_VALUE_T di_t
#define AADEQUE_EQUALS(a, b) di_equal(a, b)
#define AADEQUE_SIZE_T di_size_t
#include "aadeque.h"

// Helper. Clones an unboxed array.
static inline aadeque_t *di_aadeque_clone(aadeque_t * arr) {
	// Clone the old one and reset refc.
	arr = aadeque_clone(arr);
	arr->header.refc = 0;
	// Incref all elements in arr.
	di_size_t i;
	for (i = 0; i < aadeque_len(arr); i++)
		di_incref(aadeque_get(arr, i));
	return arr;
}

di_t di_array_empty(void) {
	di_tagged_t * a = (di_tagged_t *)aadeque_create_empty();
	di_init_tagged(a, DI_ARRAY);
	return di_from_pointer(a);
}

di_size_t di_array_length(di_t a) {
	assert(di_is_array(a));
	return aadeque_len((aadeque_t *)di_to_pointer(a));
}

di_t di_array_get(di_t a, di_size_t i) {
	assert(di_is_array(a));
	return aadeque_get((aadeque_t *)di_to_pointer(a), i);
}

di_t di_array_set(di_t a, di_size_t i, di_t v) {
	assert(di_is_array(a));
	assert(i >= 0);
	assert(i < di_array_length(a));
	aadeque_t * arr = (aadeque_t *)di_to_pointer(a);
	if (arr->header.refc > 0)
        arr = di_aadeque_clone(arr);
	// Decrement refc and possibly free the old value
	di_t oldv = aadeque_get(arr, i);
	di_decref_and_free(oldv);
	// Add and incref the new value
	aadeque_set(arr, i, v);
	di_incref(v);
	return di_from_pointer((di_tagged_t *)arr);
}

// Returns an array of length length, starting at start. The interval must be
// within valid indices of the array. Frees or reuses the memory of array if its
// ref-counter is zero.
di_t di_array_slice(di_t array, di_size_t start, di_size_t length) {
    // TODO
    return array;
}

// Concatenates two arrays. Returns the new array. Frees or reuses the memory of
// a1 and a2 if their ref-counters are zero.
di_t di_array_concat(di_t a1, di_t a2) {
    // TODO
    di_cleanup(a2);
    return a1;
}

void di_array_push(di_t * aptr, di_t v) {
	di_t a = *aptr;
	assert(di_is_array(a));
	aadeque_t * arr = (aadeque_t *)di_to_pointer(a);
	if (arr->header.refc > 0)
        arr = di_aadeque_clone(arr);
	aadeque_push(&arr, v);
	di_incref(v);
	*aptr = di_from_pointer((di_tagged_t *)arr);
}

di_t di_array_pop(di_t * aptr) {
	di_t a = *aptr;
	assert(di_is_array(a));
	aadeque_t * arr = (aadeque_t *)di_to_pointer(a);
	if (arr->header.refc > 0)
        arr = di_aadeque_clone(arr);
	di_t v = aadeque_pop(&arr);
	di_decref(v);
	*aptr = di_from_pointer((di_tagged_t *)arr);
	return v;
}

void di_array_unshift(di_t * aptr, di_t v) {
	di_t a = *aptr;
	assert(di_is_array(a));
	aadeque_t * arr = (aadeque_t *)di_to_pointer(a);
	if (arr->header.refc > 0)
        arr = di_aadeque_clone(arr);
	aadeque_unshift(&arr, v);
	di_incref(v);
	*aptr = di_from_pointer((di_tagged_t *)arr);
}

di_t di_array_shift(di_t * aptr) {
	di_t a = *aptr;
	assert(di_is_array(a));
	aadeque_t * arr = (aadeque_t *)di_to_pointer(a);
	if (arr->header.refc > 0)
		arr = di_aadeque_clone(arr);
	di_t v = aadeque_shift(&arr);
	di_decref(v);
	*aptr = di_from_pointer((di_tagged_t *)arr);
	return v;
}

/*+------+*
 *| Dict |*
 *+------+*/

/* Use oaht_t for the dict implementation */
static inline uint64_t lousy_hash(di_t v) {
	if (!di_is_pointer(v))
		return v.as_int64;
	if (di_is_string(v))
		return di_string_length(v) * 0x479 + 0xff98823;
	DIE("Only strings and numbers are allowed as dict keys");
}
#define OAHT_HEADER di_tagged_t header;
#define OAHT_KEY_T di_t
#define OAHT_KEY_EQUALS(a, b) di_equal(a, b)
#define OAHT_VALUE_T di_t
#define OAHT_SIZE_T di_size_t
#define OAHT_MIN_CAPACITY 4
#define OAHT_NO_STORE_HASH 1
#define OAHT_EMPTY_KEY di_empty()
#define OAHT_EMPTY_KEY_BYTE NANBOX_EMPTY_BYTE
#define OAHT_IS_EMPTY_KEY(key) di_is_empty(key)
#define OAHT_DELETED_KEY di_deleted()
#define OAHT_IS_DELETED_KEY(key) di_is_deleted(key)
#define OAHT_HASH(val) lousy_hash(val)
#define OAHT_HASH_T uint64_t
#include "oaht.h"

// creates an empty dict
di_t di_dict_empty(void) {
	di_tagged_t * dict = (di_tagged_t *)oaht_create();
	di_init_tagged(dict, DI_DICT);
	return di_from_pointer(dict);
}

// Returns the number of entries in the dict
di_size_t di_dict_size(di_t dict) {
	assert(di_is_dict(dict));
	struct oaht *ht = (struct oaht *)di_to_pointer(dict);
	return oaht_len(ht);
}

// True if the key exists in the dict.
bool di_dict_contains(di_t dict, di_t key) {
	assert(di_is_dict(dict));
	struct oaht *ht = (struct oaht *)di_to_pointer(dict);
	return oaht_contains(ht, key);
}

// Fetches a value from the dict. Null is returned if the key does not exist.
di_t di_dict_get(di_t dict, di_t key) {
	assert(di_is_dict(dict));
	struct oaht *ht = (struct oaht *)di_to_pointer(dict);
	return oaht_get(ht, key, di_null());
}

// Fetches the internal index given the previous internal index i. Key and value
// are pointed to the current entry. Start with i = 0. When 0 is returned, there
// are no more entries.
di_size_t di_dict_iter(di_t dict, di_size_t i, di_t *key, di_t *value) {
	assert(di_is_dict(dict));
	struct oaht *ht = (struct oaht *)di_to_pointer(dict);
	return oaht_iter(ht, i, key, value);
}

// Clones or reuses dict. Returns a dict with refc == 0.
static inline di_t di_dict_clone_or_reuse(di_t dict) {
	assert(di_is_dict(dict));
	di_tagged_t *tagged = di_to_pointer(dict);
	if (tagged->refc == 0)
		return dict; // no need to clone
	// clone
	struct oaht *ht = (struct oaht *)tagged;
	ht = oaht_clone(ht);
	ht->header.refc = 0;
	// Incref all keys and values.
	di_size_t i;
	di_t key, value;
	for (i = 0; (i = oaht_iter(ht, i, &key, &value));) {
		di_incref(key);
		di_incref(value);
	}
	// Go back to boxed pointer.
	dict = di_from_pointer((di_tagged_t *)ht);
	return dict;
}

// Associates key with value. Returns the new dict.
di_t di_dict_set(di_t dict, di_t key, di_t value) {
	assert(di_is_dict(dict));
	// Unbox
	struct oaht *ht = (struct oaht *)di_to_pointer(dict);
	// We use the special value 'empty' for a non-existing key.
	// The 'empty' value is not allowed for users so it's safe to use.
	di_t old_value = oaht_get(ht, key, di_empty());
	if (!di_is_empty(old_value) && di_equal(old_value, value)) {
		// no-op
		di_cleanup(key);
		di_cleanup(value);
                /* if (was_array && !di_is_array(value)) { */
                /*     printf("%s:%d: Dict set destroyed array\n", __FILE__, __LINE__); */
                /* } */
		return dict;
	}
	// If there are any references to it, make a clone.
	dict = di_dict_clone_or_reuse(dict);
	// Now we can edit dict. Unbox again.
	ht = (struct oaht *)di_to_pointer(dict);
	ht = oaht_set(ht, key, value);
	if (!di_is_empty(old_value)) {
		// Replacing old value. Old value deleted. No new key added.
		di_decref_and_free(old_value);
		di_cleanup(key);
	}
	else {
		// New key added. No value deleted.
		di_incref(key);
	}
	di_incref(value);
	// Go back to boxed pointer.
	dict = di_from_pointer((di_tagged_t *)ht);
	return dict;
}

// Deletes the key if it exists. Returns the new dict.
// Frees key if its refcounter is zero.
di_t di_dict_delete(di_t dict, di_t key) {
	assert(di_is_dict(dict));
	// Unbox
	struct oaht *ht = (struct oaht *)di_to_pointer(dict);
	di_t old_value = oaht_get(ht, key, di_empty());
	if (di_is_empty(old_value))
		return dict; // no-op
	// If there are any references to it, make a clone.
	dict = di_dict_clone_or_reuse(dict);
	// Unbox again
	ht = (struct oaht *)di_to_pointer(dict);

	// Delete and decref key and value
	ht = oaht_delete(ht, key);
	di_decref_and_free(key);
	di_decref_and_free(old_value);

	// Go back to boxed pointer.
	dict = di_from_pointer((di_tagged_t *)ht);
	return dict;
}

// Deletes a key from a dict and returns the value which was associated with the
// key or null if the dict didn't contain the key. Note that the dict is
// provided as a pointer which is updated to point at the updated dict. To tell
// if the key didn't exist or if it was mapped to the value null, compare the
// size of the dict before and after.
di_t di_dict_pop(di_t *dict, di_t key) {
	assert(di_is_dict(*dict));
	// Unbox
	struct oaht *ht = (struct oaht *)di_to_pointer(*dict);
	di_t old_value = oaht_get(ht, key, di_empty());
	if (di_is_empty(old_value))
		return di_null(); // no-op
	// If there are any references to it, make a clone.
	*dict = di_dict_clone_or_reuse(*dict);
	// Unbox again
	ht = (struct oaht *)di_to_pointer(*dict);

        // Instead of deleting the key, replace the value with null to make sure
        // this works inside a dict iteration.
	ht = oaht_set(ht, key, di_null());
	// Delete and Decref key and value
	//ht = oaht_delete(ht, key);
	//di_decref_and_free(key);
	di_decref(old_value);

	// Go back to boxed pointer.
	*dict = di_from_pointer((di_tagged_t *)ht);
	return old_value;
}

/*---------*
 * General *
 *---------*/

// Helper for di_equal
bool di_ptr_equal(di_t v1, di_t v2) {
	assert(di_is_pointer(v1));
	assert(di_is_pointer(v2));
	di_tagged_t *p1 = di_to_pointer(v1),
	            *p2 = di_to_pointer(v2);
	if (p1->tag != p2->tag)
		return false;
	switch (p1->tag) {
	case DI_STRING:
		{
			dynstr_t *s1 = (dynstr_t *)p1,
			         *s2 = (dynstr_t *)p2;
			return dynstr_length(s1) == dynstr_length(s2) &&
			       !memcmp(dynstr_chars(s1), dynstr_chars(s2),
			               dynstr_length(s1));
		}
	case DI_ARRAY:
		{
			aadeque_t *a1 = (aadeque_t *)p1,
				  *a2 = (aadeque_t *)p2;
			di_size_t i, n = aadeque_len(a1);
			if (aadeque_len(a2) != n)
				return false;
			for (i = 0; i < n; i++)
				if (!di_equal(aadeque_get(a1, i), aadeque_get(a2, i)))
					return false;
			return true;
		}
	case DI_DICT:
		{
			struct oaht *d1 = (struct oaht *)p1,
			            *d2 = (struct oaht *)p2;
			di_size_t i;
			if (oaht_len(d1) != oaht_len(d2))
				return false;
			for (i = 0; i <= d1->mask; i++) {
				OAHT_KEY_T key = d1->els[i].key;
				if (!OAHT_IS_EMPTY_KEY(key) &&
				    !OAHT_IS_DELETED_KEY(key) &&
				    !di_equal(d1->els[i].value,
					        oaht_get(d2, key,
					                 OAHT_EMPTY_KEY)))
					return false;
			}
			return true;
		}
	default:
		DIE("Unexpected type");
	}
}

/*-------------------------*
 * Reference-counter stuff *
 *-------------------------*/

// Non-inline helper for the inline function di_cleanup().
void di_ptr_free(di_t v) {
	assert(di_is_pointer(v));
	di_tagged_t * ptr = di_to_pointer(v);
	assert(ptr->refc == 0);
	switch (ptr->tag) {
	case DI_STRING:
		dynstr_destroy((dynstr_t *)ptr);
		break;
	case DI_ARRAY:
		// decref and free all the elements
		{
			di_size_t i;
			for (i = 0; i < di_array_length(v); i++) {
				di_t x = di_array_get(v, i);
				di_decref_and_free(x);
			}
			aadeque_destroy((aadeque_t *)ptr);
			break;
		}
	case DI_DICT:
		// decref and free all the keys and values
		{
			struct oaht *d = (struct oaht *)ptr;
			di_size_t i;
			for (i = 0; i <= d->mask; i++) {
				OAHT_KEY_T key = d->els[i].key;
				if (!OAHT_IS_EMPTY_KEY(key) &&
				    !OAHT_IS_DELETED_KEY(key)) {
					di_decref_and_free(key);
					di_decref_and_free(d->els[i].value);
				}
			}
			oaht_destroy(d);
			break;
		}
	default:
		DIE("Unexpected type");
	}
}
