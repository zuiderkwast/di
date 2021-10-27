#ifndef CLOSURE_H
#define CLOSURE_H
/*
 *--------------------------
 * Generic closure datatype
 *--------------------------
 */

/* value type for env and args */
#ifndef CLOSURE_VALUE_T
	#define CLOSURE_VALUE_T void*
#endif

/* type for lengths and indices */
#ifndef CLOSURE_SIZE_T
	#define CLOSURE_SIZE_T size_t
#endif

struct closure {
	#ifdef CLOSURE_HEADER
	CLOSURE_HEADER
	#endif
	void *fun;
	CLOSURE_SIZE_T arity;
	CLOSURE_SIZE_T envsize; /* free vars */
	CLOSURE_VALUE_T env[1];
};

/* num bytes needed to allocate for a closure with the given envsize */
static inline closure_sizeof(CLOSURE_SIZE_T envsize) {
	return sizeof(struct closure) + sizeof(CLOSURE_VALUE_T) * (envsize - 1);
}

/* init some things, but notably not the env array */
static inline void closure_init(struct closure *c, void *fun,
                                CLOSURE_SIZE_T arity, CLOSURE_SIZE_T envsize) {
	c->fun = fun;
	c->arity = arity;
	c->envsize = envsize;
}

/*
 * Example of calling a closure. This will typically be done using generated
 * code.
 *
 * If we need a library function, maybe we can use extension:
 * https://gcc.gnu.org/onlinedocs/gcc-4.0.4/gcc/Constructing-Calls.html
 * or Clang Blocks: https://clang.llvm.org/docs/BlockLanguageSpec.html
 */
static inline closure_call0(struct closure *c) {
	return (c->envsize == 0) ?
		(CLOSURE_VALUE_T (*)(void))(c->fun)() :
		(CLOSURE_VALUE_T (*)(CLOSURE_VALUE_T *))(c->fun)(c->env);
}
static inline closure_call1(struct closure *c, CLOSURE_VALUE_T arg) {
	return (c->envsize == 0) ?
		(CLOSURE_VALUE_T (*)(CLOSURE_VALUE_T))(c->fun)(arg) :
		(CLOSURE_VALUE_T (*)(CLOSURE_VALUE_T, CLOSURE_VALUE_T *))(c->fun)
			(arg, c->env);
}

#endif
