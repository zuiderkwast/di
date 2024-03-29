Todo
====

* di_error function
* Task handling (spawn, wait), task datatype
* Closure datatype (function reference)
* IO file descriptor, stream, socket, etc. datatype
* Binary type
* Type annotator (as optimization)
* Module metadata file, for access by other files
  * Exported functions with arity and possibly types
* Optimization pass on the AST?
* Compiler (to "instructions" or a pseudo-C stucture)
* Optimizer (reorder stuff, eliminate incref-decref pairs, inline?, avoid
  creating arrays when e.g. concatinating using literals, inline variables used
  only once)
* Compiling (multiple modules)
  * Generate header file
  * Generate C code
  * Intermodular dependency check (avoid need to detect cycles)
  * Generate metadata for deps without compiling them with all their deps
  * Link main module with its (compiled) depencecies
  * Check remote function references (existence, arity)
  * Type check remore function calls
* Borrowed pointer (tag pointer to avoid touching the refcounter)
  * Refactor all libs to use borrowed pointers properly.

Already done
------------

* Runtime datatypes
  * Integer, float, boolean, null, array, dict, string.
* Lexer, complete including layout rule
* Parser, basic
* Task, prototype
* Annotate var binding, access and last access

Parser todo/done
----------------

* Module top-level (todo)
  * public/export declaration or attribute (todo)
  * import (todo?)
  * Definitions
    * function def: basic (done)
    * function def: join clauses (done)
    * function def: validation of patterns and expressions (todo)
  * Expression
    * match (pattern = expr) done
    * Binop
    * Unop
    * Remote function identifier, e.g. ns::mod::func (todo)
    * Function application
    * Case
    * Do
    * If-then-else
    * Let (todo)
    * Where (todo)
    * Lambda (todo)
    * Array construction
    * Dict construction
    * Dict update
    * Literals
    * Variables
    * Validate that pattern-only stuff is not in expr context
  * Pattern
    * Variables
    * Literals
    * Regex
      * Validation by PCRE compilation (todo)
      * Detect var bindings (todo)
    * Binop
      * Validate one operand must be fixed size (todo)
    * Array
    * Dict
    * Dict update
    * Validate that there's no non-pattern stuff
