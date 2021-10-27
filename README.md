Diamant values
==============

Dynamically-typed immutable values.

Small values are stored using NaN-boxing and don't require allocation. Larger
values that require allocation are reference-counted and operations on these
make use of in-place update optimizations when possible.

Usage
-----

Include `di.h` and link with `di.o` which is compiled from `di.c`.

* When a value is used for the last time within a scope, decrement the reference
  counter of a value (`di_decref(v)`) before you pass it to a function. In-place
  update optimizations only work if the reference counter is zero.

* Functions take responsibility for freeing their arguments if the
  reference-counters are zero. Thus, if you pass a value with a zero
  reference-counter to a function, it becomes invalid afterwards if the function
  takes responsibility for freeing. Whether this applies to all function
  arguments or not is TBD. If the function is not responsible for freeing an
  argument, the caller still has the responsibity to call `di_cleanup()` or
  `di_decref_and_free()` afterwards.

* The reference-counters of newly constructed values are zero.

* Scenario:

  ```
  arr = my_arr()            # arr has refc = 0, elements in arr have refc >= 1
  di_incref(arr)            # arr refc = 1
  v = di_array_get(arr, 0)  # v has refc = 1, arr refc = 1
  # ...
  di_decref_and_free(arr)   # arr refc = 0, v refc = 0, both are freed
  print(v)                  # <--- v is already freed
  ```

  Above, the caller may not know that v is referenced from within arr and that
  freeing arr will free v. The caller needs to increment the ref-counter for v
  even though it's only used once within the function. In general, the
  ref-counter increment/decrement may be omitted only if no other functions are
  called between binding v and the last access of v.

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

Implementation
--------------

* NaN-boxing for small values (null, booleans, numbers and short strings) using
  nanbox.h
* Array
  * as array deque (aadeque.h)
  * "slice" as a pointer to another array, with length and offset (maybe TODO)
* Dict as hashtable (oaht.h)
* Function refrerence and closure
  * Allocated object with ref-counter, function-pointer, arity and closure data
    (closure.h, WIP)
  * Non-closure function references as non-allocated tagged pointers in C, with
    the arity encoded in the pointer or stored statically in the executable
    (TODO)

Function semantics
------------------

For functions that may use in-place update optimizations (such as concatenating
two strings or two arrays) the semantics described above makes sense. For other
functions such as a function for checking the length of a string, it makes more
sense that this doesn't free its argument. This is to minimize the need to
increment and decrement the reference counters of the value.

Possible solutions:

1. For each parameter to a function, we can have a flag 'f' (freeing) which
   means the function does free or reuse the value or 'n' (nonfreeing) which
   means that it doesn't. (These flags could possibly be coded into the
   function name in c, e.g. `di_t di_string_length_r(di_t str)`.)

   For user-defined functions, the *f*/*n* flag can possibly be inferred at
   compile time. For variables pointing to functions and closures, it can be
   assumed that all parameters are *n* and the function application mechanism
   has to take care of freeing values as required.

2. Introduce a pointer tag (using an unused bit in a pointer) to flag a pointer
   as "borrowed". A borrowed pointer to an object is equivalent to a regular
   pointer to the object with refrerence counter incremented by one.

   A borrowed pointer can only be used for passing arguments to functions which
   return before the variable goes out of scope, so that the caller can free it.
   A borrowed pointer can never be returned from any function.


Examples involving borrowed pointers
------------------------------------

Example 1, printing a value and then doing something else:

    v = foo()   # v has refc = 0
    print(v)    # A borrowed pointer to v is passed to print. Print doesn't free
                # the memory.
    bar(v)      # The last access of v. A regular pointer is passed. Bar takes
                # over the ownership, i.e. the responsibility to free v.

Example 2, the identity function:

    id(x) = x   # The identity function.
                # If x is a borrowed pointer, then the ref-counter of x is
                # incremented and a regular pointer is returned.

    v = foo()   # v has refc = 0
    w = id(v)   # a borrowed pointer to the object is passed to 'id' which is
                # returned with its ref-counter incremented to 1.

    (incref w)  # before calling any function on v, the reference counter of w
                # is incremented, because we can't be sure if v is refering to
                # w and thus might free v. The ref-counter is now 2.

    print(v)    # The last access of v. A regular pointer is passed and 'print'
                # decrements the ref-counter to 1.
    print(w)    # The last access of w. A regular pointer is passed and 'print'
                # decrements the ref-counter to 0 and frees the memory.
