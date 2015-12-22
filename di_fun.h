/*
 * Functions and closures for the di value system.
 */
#ifndef DI_FUN_H
#define DI_FUN_H

#include "di.h"
#include <stdarg.h>
#include <alloca.h>

#define DI_FUN   0x40


typedef struct di_fun {
	di_tagged_t header;
	void * funptr;     /* actually di_t(*funptr)(di_t, di_t, ...) */
	di_size_t arity;   /* real arity, including captured closure vars */
	di_t * cl_data;    /* closure vars as the first params to funptr */
	di_size_t cl_size; /* num closure vars */
} di_fun_t;



static inline bool di_is_fun(di_t v) {
	return di_is_pointer(v) &&
	       di_to_pointer(v)->tag == DI_FUN;
}

typedef di_t (*di_funptr0_t)(void);
typedef di_t (*di_funptr1_t)(di_t);
typedef di_t (*di_funptr2_t)(di_t, di_t);
typedef di_t (*di_funptr3_t)(di_t, di_t, di_t);
typedef di_t (*di_funptr4_t)(di_t, di_t, di_t, di_t);
typedef di_t (*di_funptr5_t)(di_t, di_t, di_t, di_t, di_t);
typedef di_t (*di_funptr6_t)(di_t, di_t, di_t, di_t, di_t, di_t);
typedef di_t (*di_funptr7_t)(di_t, di_t, di_t, di_t, di_t, di_t, di_t);
typedef di_t (*di_funptr8_t)(di_t, di_t, di_t, di_t, di_t, di_t, di_t, di_t);

static inline di_t di_call0(di_t fun) {
	assert(di_is_fun(fun));
	di_fun_t * p = (di_fun_t *)di_to_pointer(fun);
	assert (p->arity == 0);
	di_funptr0_t f = (di_funptr0_t)p->funptr;
	return f();
}

static inline di_t di_call(di_t fun, di_size_t n, ...) {
	assert(di_is_fun(fun));
	di_fun_t * f = (di_fun_t *)di_to_pointer(fun);
	if (n != f->arity - f->cl_size) {
		fprintf(stderr, "Wrong number of arguments in function call\n");
		exit(1);
	}
	va_list va;
	va_start(va, n);
	di_t * a = (di_t*)alloca(sizeof(di_t) * n);
	di_size_t i;
	for (i = 0; i < n; i++)
		a[i] = va_arg(va, di_t);
	void * p = f->funptr;
	switch (n) {
	case 0: return ((di_funptr0_t)p)();
	case 1: return ((di_funptr0_t)p)(a[0]);
	case 2: return ((di_funptr0_t)p)(a[0], a[1]);
	case 3: return ((di_funptr0_t)p)(a[0], a[1], a[2]);
	case 4: return ((di_funptr0_t)p)(a[0], a[1], a[2], a[3]);
	case 5: return ((di_funptr0_t)p)(a[0], a[1], a[2], a[3], a[4]);
	case 6: return ((di_funptr0_t)p)(a[0], a[1], a[2], a[3], a[4], a[5]);
	case 7: return ((di_funptr0_t)p)(a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
	case 8: return ((di_funptr0_t)p)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
	default:
		fprintf(stderr, "To many args.\n");
		exit(1);
	}
}

#endif
