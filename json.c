#include "json.h"
#include <assert.h>
#include <stdio.h>
#include <yajl/yajl_parse.h>
#include <yajl/yajl_gen.h>

/**
 * Parser state
 */
struct parser_state {
    /**
     * The root value.
     */
    di_t root;

    /**
     * Used while parsing objects. The key is stored here until the value
     * arrives. Then the key-value pair is added to the object on the stack.
     */
    di_t keys;

    /**
     * A parser stack. Each frame is either an array or an object.
     */
    di_t stack;
};

/**
 * Adds a value to the array or object currency being parsed. Special case for top level.
 *
 * Returns non-zero on failure.
 */
static inline int add_value(void * ctx, di_t value) {
    struct parser_state * s = (struct parser_state *)ctx;
    if (di_array_length(s->stack) == 0) {
        s->root = value;
    }
    else {
        di_size_t top_index = di_array_length(s->stack) - 1;
        di_t top = di_array_get(s->stack, top_index);
        if (di_is_array(top)) {
            di_array_push(&top, value);
        } else if (di_is_dict(top)) {
            // Pop the key from the key stack
            di_t key = di_array_pop(&s->keys);
            top = di_dict_set(top, key, value);
        } else {
            // Undexpected junk on the stack. Shouldn't happen.
            assert(false);
            return 0;
        }
        // Destructively update the stack
        s->stack = di_array_set(s->stack, top_index, top);
    }
    return 1; // OK
}

static int got_null(void * ctx) {
    return add_value(ctx, di_null());
}

static int got_boolean(void * ctx, int boolean) {
    return add_value(ctx, di_from_boolean(boolean != 0));
}

/*
// This can be used instead of int and double to parse numbers separately.
static int got_number(void * ctx, const char * s, size_t l) {
    yajl_gen g = (yajl_gen) ctx;
    return yajl_gen_status_ok == yajl_gen_number(g, s, l);
}
*/

static int got_int(void * ctx, long long intVal) {
    // FIXME: handle overflow (to double, to bigint or fail)
    return add_value(ctx, di_from_int((int32_t)intVal));
}

static int got_double(void * ctx, double doubleVal) {
    return add_value(ctx, di_from_double(doubleVal));
}

static int got_string(void * ctx, const unsigned char * stringVal,
                           size_t stringLen) {
    return add_value(ctx, di_string_from_chars((char *)stringVal, (di_size_t)stringLen));
}

static int got_map_key(void * ctx, const unsigned char * stringVal,
                       size_t stringLen) {
    struct parser_state * s = (struct parser_state *)ctx;
    di_t key = di_string_from_chars((char *)stringVal, (di_size_t)stringLen);
    di_array_push(&s->keys, key);
    return 1;
}

static int got_start_map(void * ctx) {
    struct parser_state * s = (struct parser_state *)ctx;
    di_t dict = di_dict_empty();
    di_array_push(&s->stack, dict);
    return 1;
}

static int got_end_map(void * ctx) {
    struct parser_state * s = (struct parser_state *)ctx;
    di_t dict = di_array_pop(&s->stack);
    add_value(ctx, dict);
    return 1;
}

static int got_start_array(void * ctx) {
    struct parser_state * s = (struct parser_state *)ctx;
    di_t a = di_array_empty();
    di_array_push(&s->stack, a);
    return 1;
}

static int got_end_array(void * ctx) {
    struct parser_state * s = (struct parser_state *)ctx;
    di_t a = di_array_pop(&s->stack);
    add_value(ctx, a);
    return 1;
}

static yajl_callbacks callbacks = {
    got_null,
    got_boolean,
    got_int,
    got_double,
    NULL, //got_number,
    got_string,
    got_start_map,
    got_map_key,
    got_end_map,
    got_start_array,
    got_end_array
};

di_t json_decode(di_t json) {
    assert(di_is_string(json));
    char * json_chars = di_string_chars(json);
    size_t json_len   = di_string_length(json);
    struct parser_state s;
    s.keys  = di_array_empty();
    s.stack = di_array_empty();
    yajl_handle parser = yajl_alloc(&callbacks, NULL, &s);
    //yajl_status status;
    if (yajl_parse(parser, (unsigned char *)json_chars, json_len) != yajl_status_ok
        || yajl_complete_parse(parser) != yajl_status_ok) {
        // Parse error. Return undefined.
		#ifdef JSON_DEBUG
        printf("JSON parse error.\n");
		#endif
        s.root = di_undefined();
    }
    else {
        // These should both be emtpy after successful parse.
        assert(di_array_length(s.keys) == 0);
        assert(di_array_length(s.stack) == 0);
    }
    di_cleanup(s.keys);
    di_cleanup(s.stack);
    yajl_free(parser);
    di_cleanup(json);
    return s.root;
}

/**
 * recursive helper for json_encode. Returns 1 on success, 0 on failure.
 */
int encode_rec(yajl_gen g, di_t v) {
    yajl_gen_status status;
    if (di_is_null(v)) {
        status = yajl_gen_null(g);
    } else if (di_is_boolean(v)) {
        status = yajl_gen_bool(g, di_to_boolean(v));
    } else if (di_is_int(v)) {
        status = yajl_gen_integer(g, di_to_int(v));
    } else if (di_is_double(v)) {
        status = yajl_gen_double(g, di_to_double(v));
    } else if (di_is_string(v)) {
        status = yajl_gen_string(g, (unsigned char *)di_string_chars(v), di_string_length(v));
        if (!di_is_string(v)) printf("Not string anymore!!!!!\n");
    } else if (di_is_array(v)) {
        di_size_t i;
        di_size_t n = di_array_length(v);
        status = yajl_gen_array_open(g);
        if (status != yajl_gen_status_ok)
            //return 0;
            di_error(di_string_from_cstring("Failed to open array with '['\n"));
        for (i = 0; i < n; i++) {
            if (!encode_rec(g, di_array_get(v, i))) {
                //return 0;
                printf("Array elem %u of %u\n", i, n);
                di_error(di_string_from_cstring("Failed to stringify value in array\n"));
            }
        }
        status = yajl_gen_array_close(g);
    } else if (di_is_dict(v)) {
        di_size_t i;
        di_t k, val;
        status = yajl_gen_map_open(g);
        if (status != yajl_gen_status_ok)
            di_error(di_string_from_cstring("Failed to open map with '{'"));
            //return 0;
        for (i = 0; (i = di_dict_iter(v, i, &k, &val));) {
            if (!di_is_string(k)) {
            	di_error(di_string_from_cstring("Non-string key found in dict -"
            	                                " can't convert to JSON"));
            	//exit(1);
            }
            assert(di_is_string(k));
            status = yajl_gen_string(g, (unsigned char *)di_string_chars(k), di_string_length(k));
            if (status != yajl_gen_status_ok) {
                //return 0;
                di_error(di_string_from_cstring("Failed to stringify key in dict\n"));
            }
            if (!encode_rec(g, val))
                return 0;
        }
        status = yajl_gen_map_close(g);
    }
    else {
        // Type not serializable in JSON
        //assert(false);
        printf("Non-serializable value %p\n", (void*)(v.as_int64));
        #ifdef JSON_DEBUG
		printf("Type not JSON serializable.\n");
        #endif
        return 0;
    }
    if (status != yajl_gen_status_ok)
    	printf("YAJL gen status: %d\n", status);
    return status == yajl_gen_status_ok;
}

/** a callback used for "printing" the results. */
void print_callback(void * ctx, const char * str, size_t len) {
    di_t *s = (di_t *)ctx;
    *s = di_string_append_chars(*s, str, len);
}

di_t json_encode(di_t value) {
    yajl_gen g = yajl_gen_alloc(NULL);
    di_t result = di_string_empty();
    yajl_gen_config(g, yajl_gen_print_callback, print_callback, &result);
    int success = encode_rec(g, value);
    yajl_gen_free(g);
    if (!success) {
        // json_encode failed
        // TODO: handle in a better way
        di_cleanup(result);
        return di_undefined();
    }
    return result;
}
