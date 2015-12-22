Diamant values
==============

Dynamically-typed immutable values.

Small values are stored using NaN-boxing and don't require allocation. Larger
values that require allocation are reference-counted and operations on these
make use of in-place update optimizations when possible.

Usage
-----

Include `di.h` and link with `di.o` which is compiled from `di.c`.

* Decrement the reference-counter of a value (`di_decref(v)`) before you pass
  it to a function. In-place update optimizations only work if the
  reference-counter is zero.
* Some functions take responsibility for freeing their arguments if the
  reference-counters are zero. Thus, if you pass a value with a zero
  reference-counter to a function, it becomes invalid afterwards if the
  function takes responsibility for freeing. If the function is not responsible
  for freeing, you have to call `di_cleanup()` or `di_decref_and_free()`.
* The reference-counters of newly constructed values are zero.

Types
-----

* null
* boolean
* integer
* double
* string
* array
* dict
* function (TODO)

Implementations
---------------

* NaN-boxing for small values (null, booleans, numbers and short strings) using
  nanbox.h
* Array
  * as array deque (aadeque.h)
  * "slice" as a pointer to another array, with length and offset (TODO)
* Dict as hash-table (oaht.h)
* Function as ... (TODO)
  * Allocated object with ref-counter, function-pointer, arity and closure data
   (TODO)
  * Non-closure function references as non-allocated static tagged values in c
   (TODO)

Function semantics
------------------

For functions that may use in-place update optimizations (such as concatenating
two strings or two arrays) the semantics described above makes sense. For other
functions such as function for checking the length of a string, it makes more
sense that this doesn't free its argument. This is to minimize the need to
increment and decrement the reference counters of the value.

For each parameter to a function, we can have a flag 'f' (freeing) which means
the function does free or reuse the value or 'n' (nonfreeing) which means that
it doesn't. (These flags could possibly be coded into the function name in c,
e.g. `di_t di_string_length_r(di_t str)`.)

For user-defined functions, the *f*/*n* flag can possibly be inferred at compile
time. For variables pointing to functions and closures, it can be assumed that
all parameters are *n* and the function application mechanism has to take care
of freeing values as required.

