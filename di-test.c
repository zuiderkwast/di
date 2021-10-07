/*
 * Compile and link with di.c
 */

/* A minimalistic unit test framework in 4 lines of code pasted in here */

/* file: minunit.h */
#define mu_assert(message, test) do { if (!(test)) return message; } while (0)
#define mu_run_test(test) do { char *message = test(); tests_run++; \
                               if (message) return message; } while (0)
extern int tests_run;
/* ---- */

#include <stdlib.h>
#include <stdio.h>

#include "di.h"

typedef char *(*testfun)(void);
int tests_run;

static char * string_test(void) {
	char * strings[2] = {"foo-bar-baz", "foo"};
	int i;
	for (i = 0; i < 2; i++) {
		di_t s = di_string_from_cstring(strings[i]);
		mu_assert("string is string", di_is_string(s));
		mu_assert("string is not array", !di_is_array(s));
		mu_assert("string is not dict", !di_is_dict(s));
		mu_assert("string is not int", !di_is_int(s));
		di_cleanup(s);
	}
	return NULL;
}

static char * string_from_cstring_test(void) {
    char buf[100];
    char c = 'a';
    int l = 0;
    while (l < 100) {
        buf[l] = '\0';
        di_t s = di_string_from_cstring(buf);
        mu_assert("string lengths match",
                  di_string_length(s) == l);
        mu_assert("string contents match",
                  strncmp(di_string_chars(s), buf, l) == 0);
        di_cleanup(s);
        // append a char to buf "abcd..."
        buf[l++] = c++;
        if (c > 'z') c = 'a';
    }
    return NULL;
}

//static char * string_append_test(void) {
    //di_t s = di_string_from_cstring(

static char * array_set_test(void) {
	di_t a = di_array_empty();
	di_t b;
	di_array_push(&a, di_null());
	mu_assert("array has 1 element", di_array_length(a) == 1);
	di_incref(a);
	b = di_array_set(a, 0, di_true());
	mu_assert("Non-destructive set", !di_equal(a, b));
	di_cleanup(b);
	di_decref_and_free(a);
	return NULL;
}

static char * array_push_inplace_test(void) {
	di_t a = di_array_empty();
	di_t b = a;
	mu_assert("array is empty", di_array_length(a) == 0);
	di_array_push(&b, di_null());
	mu_assert("array push 1 element", di_array_length(b) == 1);
	mu_assert("array push in-place", di_equal(a, b));
	di_cleanup(a);
	return NULL;
}

static char * array_push_clone_test(void) {
	di_t a = di_array_empty();
	di_incref(a);
	di_t b = a;
	mu_assert("array is empty", di_array_length(b) == 0);
	di_array_push(&b, di_null());
	mu_assert("array push 1 element", di_array_length(b) == 1);
	mu_assert("array original is still empty", di_array_length(a) == 0);
	mu_assert("array push not in-place", &a != &b);
	di_decref_and_free(a);
	di_cleanup(b);
	return NULL;
}

testfun tests[] = {
	string_test,
        string_from_cstring_test,
	array_set_test,
	array_push_inplace_test,
	array_push_clone_test
};

char * run_tests(void) {
	tests_run = 0;
	int i;
	for (i = 0; i < sizeof(tests) / sizeof(void *); i++) {
		char *(*test)(void) = tests[i];
		//char *(*test)(void) = &string_test; //tests[i];
		//testfun test = string_test;
		//test();
		mu_run_test(test);
	}
	return (char *)0;
}

int main() {
	char * message;
	message = run_tests();
	if (message) {
		printf("Failed asserting that %s.\n", message);
		return 1;
	}
	printf("OK\n");
	return 0;
}
