/*
  di_annotate(): A pass after di_parse() which does a number of things:

  - Check that variables are bound before they are accessed.

  - Check that closures not accessed before any of the variables they depend on
    for their environment are bound.

  - Annotate the parse tree with variable binds and accesses (varset = the set
    of variables used within an expression or pattern; action = variable action
    (expr=var only): bind/discard/access/last).

      - Bind means that the variable is bound in this pattern.

      - Discard means that the variable is bound but never accessed, so it can be
        discarded instantly. There is an "unused variable" warning (or error)
        unless the variable starts with an underscore.

      - First means the guaranteed first access of a bound variable. (Here, the
        reference counter is incremented.)

      - Last means the guaranteed last access of a bound variable. (Here, there
        reference counter is decremented.)

      - Only means the only (first and last) access of a bound variable.

      - Access means any access of a bound variable which is not guaranteed to
        be the first nor the last.

  - Every access to a closure counts as an access to each of the variables
    captured in its environment. In practice, we'll only instanciate the closure
    once, but we don't infer conditional accesses, so currently we accept this
    limitation.

  - Basic type annotations (TODO?)

  The parse tree is returned with annotations added. If an error is found, it's
  reported using di_error().

  Terminology used in this file:

  Set = a dict with elements as keys and null as values.

  Varset = a dict of variables with their names as the keys and their access
  types as values. The access type is initially set to "bind" for a variable
  bound in a pattern, "first" for the first access to the bound variable or
  "access" any other access to a variable. In a later pass, the last occurrence
  of "access" may be replaced by "last" or "only" if it's both the first and the
  last access. "Bind" may be replaced by "discard" if a variable is never
  accessed.

  Scope = a dict with variable/function names as keys. The values are only used
  in the case of functions. For function, the value is a dict where the keys are
  the variables captured in the closure environment. All the captured variables
  are marked as accessed in the expression. For variables other than functions,
  the value is non-null (for example true).

  Nested scope = an array of scopes. The innermost scope is the first element
  in the array.
*/

/*

  How do we detect the first/last/only access of a variable?

  Example using the expressions E1, E2, E3 and (if E1 then E2 else E3). E1 is
  always evaluated but E2 and E3 are only conditionally evaluated.

      x = foo()                         -- x is bound
      y = if bar() then x else 42       -- first access of x
      print(x)                          -- last access of x

  If the first access is conditional, we still treat it as the first access (and
  increment the reference counter in the generated code). With reference-count
  operation inserted, the code looks like this:

      x = foo()
      incref(x)                         -- increment before the first access
      y = if bar() then x else 42
      decref(x)                         -- decrement before the last access
      print(x)                          -- ownership is transferred to print

  If the last access is conditional, we need to drop the value in all branches
  which don't access the variable. Example:

      x = foo()                         -- x is bound
      print(x)                          -- first access of x
      y = if bar() then baz(x) else 42  -- last access of x

  The same example With reference-counter operations inserted:

      x = foo()
      incref(x)                         -- increment before the first access
      print(x)
      y = if bar() then do decref(x)    -- decrement befor the last access
                           baz(x)       -- ownership transferred to baz
                   else do drop(x)      -- drop in branches where not accessed
                           42

  If all accesses to a variable is contained within a single expression, the
  operations are pushed to the sub-expressions. In the following example, all
  accesses to x are contained within the do-block:

      x = foo()
      do y = x
         bar(x)
         baz(y)

  For functions (closures), the first access is where the environment is
  created, so the variables captured by the closure are accessed here.
  Subsequent acesses of the function only count as accesses of the function
  itself.

  We need one pass to infer where each variable is bound and where it's
  accessed. We need another pass to infer the first access (or if there is none,
  to indicate that where the variable is bound and to give an "unused variable"
  warning) and a third pass to infer the last access of each variable. During
  the last access pass, if the first access is found to be the last access, it
  is indicated as the only access. The last and only labels are given only if it
  is unconditionally the last access. Otherwise, it is simply labelled as an
  access.

  TODO
  ----

  - Fix all simple TODOs marked in the code.

  - Fix union and diff of varsets, e.g. merge of "bind" and "access". (For the
    purpose of marking first and last access of each variable, bind u access =
    access.

  - Fix varset when exiting a local scopes. Varset = accesses \ local scope.

  - Rename "varset" to e.g. "vars". (It's a dict, not a set.)

*/

#include "di_annotate.h"
#include "di_debug.h"
#include <stdarg.h>
#include <stdio.h>

static di_t create_block_scope(di_t es);
static di_t block(di_t es, di_t *scopes);
static di_t expr_or_let(di_t e, di_t *scopes);
static di_t exprs(di_t es, di_t *scopes);
static di_t expr(di_t e, di_t *scopes);
static di_t patterns(di_t ps, di_t *scopes);
static di_t pattern(di_t p, di_t *scopes);
static di_t clauses(di_t cs, di_t *scopes);

static bool mark_last_access_in_seq(di_t *es, di_t varname);
static bool mark_last_access(di_t *e, di_t varname);
static void mark_last_accesses(di_t *e, di_t varset);

static di_t varset_union(di_t e1, di_t e2);
static di_t varset_union3(di_t e1, di_t e2, di_t e3);
static di_t set_varset(di_t e, di_t varset);
static di_t get_varset(di_t e); // e: dict with a "varset" key or array of such dicts

// Set datatype using dicts, ignoring the values (FIXME we need the values...)
static di_t setunion(di_t a, di_t b);
//static di_t setinter(di_t a, di_t b);
static di_t dict_diff(di_t a, di_t b);

static void error_expr_format(di_t e, const char *format, ...);

// Simple construction of di strings
#define str(arg) di_string_from_cstring(arg)

// For use with printf functions
#define PRIstr "%.*s"
#define FMTstr(s) di_string_length(s), di_string_chars(s)

// Nested scope to set (list of dicts => dict)
#define UNUSED inline // prevents unused warning
static UNUSED di_t nested_scope_to_set(di_t scopes) {
    di_t set = di_dict_empty();
    di_size_t n = di_array_length(scopes);
    for (di_size_t i = 0; i < n; i++) {
        set = setunion(set, di_array_get(scopes, i));
    }
    return set;
}

// Lookup a variable name in a nested scope. Returns null if the variable isn't
// found.
static di_t lookup_nested_scope(di_t name, di_t *scopes) {
    di_size_t n = di_array_length(*scopes);
    for (di_size_t i = 0; i < n; i++) {
        di_t scope = di_array_get(*scopes, i);
        di_t value = di_dict_get(scope, name);
        if (!di_is_null(value))
            return value;
    }
    return di_null();
}

di_t di_annotate(di_t ast) {
    di_t scopes = di_array_empty();
    if (!di_equal(str("do"), di_dict_get(ast, str("syntax")))) {
        di_error(str("Unexpected parse tree. A block is expected on top level."));
    }
    ast = block(ast, &scopes);
    di_cleanup(scopes);
    return ast;
}

// Checks and annotates a function definition, possibly with multiple clauses
// e.g. `f(0,x) = x; f(x,y) = x+y`. Keys that are added to def:
//
// - "env": a dict of variables captured from the surrounding scope (values
//   unspecified)
// - "varset": added in each clause and in expressions and patterns
static di_t funcdef(di_t def, di_t *scopes) {
    di_t cs = di_dict_get(def, str("clauses"));
    cs = clauses(cs, scopes);
    di_t env = get_varset(cs);
    def = di_dict_set(def, str("clauses"), cs);
    def = di_dict_set(def, str("env"), env); // TODO? rename "env" to "varset"?
    return def;
}

// The top-level sequence of expressions and definitions or the body of a 'do'
// expression. Returns an annotated one. The nested scope is not modified, since
// variables are bound in an inner scope which is gone when the function return.
static di_t block(di_t block, di_t *scopes) {
    di_t defs = di_dict_pop(&block, str("defs"));

    // Create a scope with the function definitions in this block first. They
    // can be defined in any order.
    di_t scope = create_block_scope(defs);
    di_array_unshift(scopes, scope);

    // Check the function defintions and get their closure environments, i.e.
    // accesses to variables outside their local scope, so we can check that all
    // variables are defined before the closure is accessed.
    //
    //     map(f, xs)     -- Error: Can't use f before y is bound
    //     y = 2
    //     f(x) = x + y
    //
    di_t name;
    for (int i = 0; (i = di_dict_iter(defs, i, &name, NULL)) != 0;) {
        di_t def = di_dict_pop(&defs, name);
        def = funcdef(def, scopes);
        defs = di_dict_set(defs, name, def);

        // Update the function's entry in the local scope reflect the variables
        // the function depends on (if any), so we can check that we don't
        // access the closure before their environment variables are bound.
        di_t env = di_dict_get(def, str("env"));
        di_t scope = di_array_shift(scopes);
        di_t scope_value = di_is_null(env) ? di_dict_empty() : env;
        scope = di_dict_set(scope, name, scope_value);
        di_array_unshift(scopes, scope);
    }
    block = di_dict_set(block, str("defs"), defs);

    // The sequence of expressions including let (or match) expressions
    di_t es = di_dict_pop(&block, str("seq"));
    for (di_size_t i = 1, n = di_array_length(es); i <= n; i++) {
        di_t e = di_array_shift(&es);
        e = expr_or_let(e, scopes); // this differs from exprs()
        di_array_push(&es, e);
    }

    // End of the variable scope. Mark the first (TODO) and last accesses of
    // each of the variables that go out of scope. Detect unused variables.
    scope = di_array_shift(scopes);
    di_size_t j = 0;
    di_t varname;
    while ((j = di_dict_iter(scope, j, &varname, NULL)) != 0) {
        bool found = mark_last_access_in_seq(&es, varname);
        if (!found) {
            di_debug("Failed to mark last access of ", varname);
            di_debug("... in seq ", es);
            di_debug("... where outer scopes are ", *scopes);
        }
        assert(found);
    }

    // Set varset, the accesses of variables bound outside the block, to that of
    // seq (defs are included in seq) minus the local scope.
    di_t varset = dict_diff(get_varset(es), scope);
    block = set_varset(block, varset);

    block = di_dict_set(block, str("seq"), es);

    return block;
}

// a sequence of expressions, such as the args in a function call.
static di_t exprs(di_t es, di_t *scopes) {
    for (di_size_t i = 0, n = di_array_length(es); i < n; i++) {
        di_t e = di_array_shift(&es);
        e = expr(e, scopes);
        di_array_push(&es, e);
    }
    return es;
}

// a sequence of patterns, such as the parameters in a function definition.
static di_t patterns(di_t ps, di_t *scopes) {
    for (di_size_t i = 0, n = di_array_length(ps); i < n; i++) {
        di_t p = di_array_shift(&ps);
        p = pattern(p, scopes);
        di_array_push(&ps, p);
    }
    return ps;
}

// x = y is not really an expression. It is only allowed in a do block and on
// top-level. A sequence on the form `x = y; e` means `let x = y in e`.
static di_t expr_or_let(di_t e, di_t *scopes) {
    di_t op = di_dict_get(e, str("syntax"));
    if (di_equal(op, str("="))) {
        di_t left = di_dict_get(e, str("left"));
        di_t right = di_dict_get(e, str("right"));
        // LHS is a pattern which binds variables in current scope, but not
        // in the scope of RHS! (That's letrec and we don't have that.)
        // RHS: expression
        right = expr(right, scopes);
        left = pattern(left, scopes);
        e = set_varset(e, varset_union(left, right));
        e = di_dict_set(e, str("left"), left);
        e = di_dict_set(e, str("right"), right);
    } else {
        e = expr(e, scopes);
    }
    return e;
}

static bool is_logicop(di_t op) {
    return di_equal(op, str("and")) || di_equal(op, str("or"))
        || di_equal(op, str("not"));
}
static bool is_relop(di_t op) {
    return di_equal(op, str("<")) || di_equal(op, str(">"))
        || di_equal(op, str("=<")) || di_equal(op, str(">="))
        || di_equal(op, str("==")) || di_equal(op, str("!="));
}
static bool is_arithop(di_t op) {
    char *s = di_string_chars(op);
    return strchr("+-*/", s[0]) != NULL || di_equal(op, str("mod"));
}
static bool is_unop(di_t op) {
    return di_equal(op, str("-")) || di_equal(op, str("not"));
}
// Note: "=" is not included.
static bool is_operator(di_t op) {
    return is_logicop(op) || is_relop(op) || is_arithop(op)
        || di_equal(op, str("~")) || di_equal(op, str("@"));
}

// clause = {"pats": [pattern], "body": expr}.
//
// The patterns bind variables in a local scope. This function adds a "varset"
// key to each clause, containing only the vars with a scope outside the
// clauses.
static di_t clauses(di_t cs, di_t *scopes) {
    di_size_t n = di_array_length(cs);
    for (di_size_t i = 0; i < n; i++) {
        di_t c = di_array_shift(&cs);
        // Push local scope
        di_t scope = di_dict_empty();
        di_array_unshift(scopes, scope);
        // Patterns bind vars in local scope
        di_t pats = di_dict_pop(&c, str("pats"));
        di_t body = di_dict_pop(&c, str("body"));
        di_debug("Scopes clause patterns: ", *scopes);
        pats = patterns(pats, scopes);
        di_debug("Scopes before body: ", *scopes);
        body = expr(body, scopes);
        // Pop local scope
        scope = di_array_shift(scopes);
        di_incref(scope);
        // First set varset of clause including local scope (we'll remove it later)
        di_t varset = varset_union(pats, body);
        c = set_varset(c, varset);
        c = di_dict_set(c, str("pats"), pats);
        c = di_dict_set(c, str("body"), body);
        // mark last accesses
        mark_last_accesses(&c, scope);
        // TODO: mark first access
        // Varset of clause = varset of pats and body minus local scope
        varset = di_dict_pop(&c, str("varset"));
        di_decref(scope);
        varset = dict_diff(varset, scope);
        c = set_varset(c, varset);
        di_array_push(&cs, c);
    }
    return cs;
}

// Checks the key-value entries and adds a "varset" key to each entry dict.
static di_t dict_entries(di_t entries, di_t *scopes,
                         di_t (*pattern_or_expr)(di_t, di_t*)) {
    for (di_size_t i = 0, n = di_array_length(entries); i < n; i++) {
        di_t entry = di_array_shift(&entries);
        assert(di_equal(di_dict_get(entry, str("syntax")), str("entry")));

        di_t key = di_dict_pop(&entry, str("key"));
        key = pattern_or_expr(key, scopes);
        entry = di_dict_set(entry, str("key"), key);

        di_t value = di_dict_pop(&entry, str("value"));
        value = pattern_or_expr(value, scopes);
        entry = di_dict_set(entry, str("value"), value);

        entry = set_varset(entry, varset_union(key, value));
        di_array_push(&entries, entry);
    }
    return entries;
}

// returns the union of the varset of two expressions or arrays of expressions.
// (Does not free the arguments.)
//
// FIXME 1: Merge accesses properly.
// FIXME 2: Use the empty dict for the empty varset.
static di_t varset_union(di_t e1, di_t e2) {
    return setunion(get_varset(e1), get_varset(e2));
}

static di_t varset_union3(di_t e1, di_t e2, di_t e3) {
    return setunion(setunion(get_varset(e1), get_varset(e2)), get_varset(e3));
}

// Sets the "varset" key of expression e to varset, or deletes the key if varset
// is null. (TODO: Add immediate value for empty dict and empty array? Then we
// can just use that instead of a null hack.)
static di_t set_varset(di_t e, di_t varset) {
    if (di_is_null(varset))
        return di_dict_delete(e, str("varset"));
    else
        return di_dict_set(e, str("varset"), varset);
}

// Returns the value of the "varset" key of a dict. For an array, we take the
// varset of each child and merge them.
static di_t get_varset(di_t e) {
    if (di_is_array(e)) {
        // For array, we get_varset on the elements and merge the results.
        di_t merged = di_null();
        di_size_t i, n = di_array_length(e);
        for (i = 0; i < n; i++) {
            di_t elem = di_array_get(e, i);
            di_t varset = di_dict_get(elem, str("varset"));
            merged = setunion(merged, varset);
        }
        return merged;
    } else if (di_is_dict(e)) {
        return di_dict_get(e, str("varset"));
    } else {
        di_debug("get_varset() invalid arg ", e);
        assert(0);
        return di_empty();
    }
}

// Adds all recursively accessed vars to varset. Varset_acc are the vars already
// explored, so we can avoid going into loops. If any variable is free, an
// "undefined variable" error is raised.
di_t get_rec_accessed_varset(di_t name, di_t *scopes, di_t varset_acc, di_t orig_expr) {
    /* di_debug("hello from get_rec_accessed_varset ", name); */
    /* di_debug("*scopes = ", *scopes); */
    /* di_debug("varset_acc = ", varset_acc); */
    /* di_debug("orig_expr = ", orig_expr); */
    if (di_dict_contains(varset_acc, name)) {
        return varset_acc; // We've already explored this path.
    }
    di_t scope_value = lookup_nested_scope(name, scopes);
    /* di_debug("scope_value = ", scope_value); */
    if (di_is_null(scope_value)) {
        error_expr_format(orig_expr, "Undefined variable "PRIstr, FMTstr(name));
    }
    varset_acc = di_dict_set(varset_acc, name, str("access"));
    if (di_is_dict(scope_value)) {
        // This is a function. Here, the closure is instanciated (if it's not
        // already instantiated, which we only know at runtime) and the closure
        // variables are thereby possibly accessed.
        //
        //     somevar = ["some", "data"]
        //     if a then map(f, xs)         -- maybe instanciate f
        //          else null               -- (access somevar and othervar)
        //     if b then map(f, ys)         -- maybe instanciate f
        //          else null               -- (access somevar and othervar)
        //     f(x) = [x, somevar, g()]
        //     g() = [othervar]
        //
        di_t key;
        for (di_size_t i = 0; (i = di_dict_iter(scope_value, i, &key, NULL)) != 0;) {
            varset_acc = get_rec_accessed_varset(key, scopes, varset_acc, orig_expr);
        }
    }
    return varset_acc;
}

static di_t expr(di_t e, di_t *scopes) {
    di_t op = di_dict_get(e, str("syntax"));
    /* di_debug("expr ", op); */
    if (is_operator(op)) {
        di_t right = di_dict_pop(&e, str("right"));
        right = expr(right, scopes);
        di_t left = di_dict_pop(&e, str("left"));
        if (di_is_null(left)) {
            assert(is_unop(op));
            e = set_varset(e, get_varset(right));
        } else {
            left = expr(left, scopes);
            e = set_varset(e, varset_union(left, right));
            e = di_dict_set(e, str("left"), left);
        }
        e = di_dict_set(e, str("right"), right);
    } else if (di_equal(op, str("apply"))) {
        di_t func = expr(di_dict_pop(&e, str("func")), scopes);
        di_t args = exprs(di_dict_pop(&e, str("args")), scopes);
        e = set_varset(e, varset_union(func, args));
        e = di_dict_set(e, str("func"), func);
        e = di_dict_set(e, str("args"), args);
    } else if (di_equal(op, str("case"))) {
        di_t subj = di_dict_pop(&e, str("subj"));
        subj = expr(subj, scopes);
        di_t cs = di_dict_pop(&e, str("clauses")); // [{"pats": [pat], "body": expr}]
        cs = clauses(cs, scopes);
        e = set_varset(e, varset_union(subj, cs));
        e = di_dict_set(e, str("subj"), subj);
        e = di_dict_set(e, str("clauses"), cs);
    } else if (di_equal(op, str("do"))) {
        e = block(e, scopes);
    } else if (di_equal(op, str("if"))) {
        di_t cond = expr(di_dict_pop(&e, str("cond")), scopes);
        di_t if_then = expr(di_dict_pop(&e, str("then")), scopes);
        di_t if_else = expr(di_dict_pop(&e, str("else")), scopes);
        e = set_varset(e, varset_union3(cond, if_then, if_else));
        e = di_dict_set(e, str("cond"), cond);
        e = di_dict_set(e, str("then"), if_then);
        e = di_dict_set(e, str("else"), if_else);
    } else if (di_equal(op, str("array"))) {
        di_t elems = di_dict_pop(&e, str("elems"));
        elems = exprs(elems, scopes);
        e = set_varset(e, get_varset(elems));
        e = di_dict_set(e, str("elems"), elems);
        /* if (!di_is_array(elems)) { */
        /*     printf("%s:%d: Dict set destroyed array.\n", __FILE__, __LINE__); */
        /* } */
    } else if (di_equal(op, str("dict"))) {
        // entries = [{"syntax": "entry", "key":pattern, "value":expr}]
        di_t entries = di_dict_pop(&e, str("entries"));
        entries = dict_entries(entries, scopes, &expr);
        e = set_varset(e, get_varset(entries));
        e = di_dict_set(e, str("entries"), entries);
    } else if (di_equal(op, str("dictup"))) {
        // subj{k: v, entries...}
        // subj = expr
        // entries = [{"syntax":"entries", "key":pattern, "value":expr}, ...]
        di_t subj = di_dict_pop(&e, str("subj")); // expr
        subj = expr(subj, scopes);
        di_t entries = di_dict_pop(&e, str("entries"));
        entries = dict_entries(entries, scopes, &expr);
        e = set_varset(e, varset_union(subj, entries));
        e = di_dict_set(e, str("subj"), subj);
        e = di_dict_set(e, str("entries"), entries);
    } else if (di_equal(op, str("var"))) {
        di_t name = di_dict_get(e, str("name"));
        // Check if var (and any other var it depends on) is in scope.
        di_t varset = get_rec_accessed_varset(name, scopes, di_dict_empty(), e);
        e = di_dict_set(e, str("action"), str("access"));
        e = di_dict_set(e, str("varset"), varset); // set of accessed variables
    } else if (di_equal(op, str("lit"))) {
        // value
        return e;
    } else if (di_equal(op, str("regex"))) {
        // regex (ERROR! only allowed in patterns)
        error_expr_format(e, "Regular expression can't be used in this context.");
    } else {
        // error
        error_expr_format(e, "Unknown expression");
    }
    return e;
}

static void mark_last_accesses(di_t *e_ptr, di_t varset) {
    di_t varname;
    for (di_size_t i = 0; (i = di_dict_iter(varset, i, &varname, NULL)) != 0;) {
        bool found = mark_last_access(e_ptr, varname);
        if (!found) {
            di_debug("Last access not found for var ", varname);
            di_debug("... in ... ", *e_ptr);
        }
        assert(found);
    }
    di_cleanup(varset);
}

// Marks the last access to a variable in a sequence of syntax elements
// (expressions, patterns, clauses or entries).
// A boolean is returned indicating if any access was found. If true, the es
// (expression array) pointer is updated with the last access marked.
static bool mark_last_access_in_seq(di_t *es, di_t varname) {
    // Loop over the expressions backwards. Where the variable last occurs is
    // the last access.
    di_size_t n = di_array_length(*es);
    // Note that i is unsiged so i >= 0 is always true.
    for (di_size_t i = n; i-- > 0;) {
        di_t e = di_array_get(*es, i);
        di_t varset = di_dict_get(e, str("varset"));
        if (di_is_dict(varset) && di_dict_contains(varset, varname)) {
            // The last access of the variable is somewhere inside e. Take out e
            // from es to avoid to enable in-place updates of e without copying.
            di_incref(e);
            *es = di_array_set(*es, i, di_null());
            di_decref(e);
            bool success = mark_last_access(&e, varname);
            assert(success);
            *es = di_array_set(*es, i, e);
            return true;
        }
    }
    return false;
}

// Updates the "action" in the "var" nodes for the last occurrence of the
// variable in the following way "bind" => "discard" (meaning the variable is
// bound but never used) and "access" => "last" (meaning it's the last access of
// the variable).
//
// Currently, the value of "varset" keys are is not updated. Do we need to do that?
static bool mark_last_access(di_t *e_ptr, di_t varname) {
    di_t e = *e_ptr;
    di_t op = di_dict_get(e, str("syntax"));
    di_t varset = di_dict_get(e, str("varset"));
    if (di_is_null(varset) || !di_dict_contains(varset, varname))
        return false; // Variable not accessed in this branch

    if (di_equal(op, str("var")) &&
        di_equal(di_dict_get(e, str("name")), varname)) {

        di_t action = di_dict_pop(&e, str("action"));
        if (di_equal(action, str("access"))) {
            action = str("last");
        } else if (di_equal(action, str("bind"))) {
            // TODO: Warning or error for unused variable (except if it starts
            // with an underscore).
            printf("TODO: %d:%d: Warning: Unused variable '" PRIstr "'\n",
                   di_to_int(di_dict_get(e, str("line"))),
                   di_to_int(di_dict_get(e, str("column"))),
                   FMTstr(varname));
            action = str("discard");
        } else {
            assert(0); // The only possibilities are "access" and "bind" here.
        }
        e = di_dict_set(e, str("action"), action);
    } else if (di_equal(op, str("regex"))) {
        // FIXME
    } else if (di_equal(op, str("="))) {
        di_t left = di_dict_pop(&e, str("left"));
        if (!mark_last_access(&left, varname)) {
            di_t right = di_dict_pop(&e, str("right"));
            assert(mark_last_access(&right, varname));
            e = di_dict_set(e, str("right"), right);
        }
        e = di_dict_set(e, str("left"), left);
    } else if (is_operator(op)) {
        di_t right = di_dict_pop(&e, str("right"));
        if (!mark_last_access(&right, varname)) {
            di_t left = di_dict_pop(&e, str("left"));
            assert(!di_is_null(left)); // unary operators have no left operand.
            assert(mark_last_access(&left, varname));
            e = di_dict_set(e, str("left"), left);
        }
        e = di_dict_set(e, str("right"), right);
    } else if (di_equal(op, str("if"))) {
        di_t if_then = di_dict_pop(&e, str("then"));
        di_t if_else = di_dict_pop(&e, str("else"));
        bool last_then = mark_last_access(&if_then, varname);
        bool last_else = mark_last_access(&if_else, varname);
        e = di_dict_set(e, str("then"), if_then);
        e = di_dict_set(e, str("else"), if_else);
        if (!last_then && !last_else) {
            di_t cond = di_dict_pop(&e, str("cond"));
            assert(mark_last_access(&cond, varname));
            e = di_dict_set(e, str("cond"), cond);
        }
    } else if (di_equal(op, str("case"))) {
        di_t cs = di_dict_pop(&e, str("clauses"));
        if (!mark_last_access_in_seq(&cs, varname)) {
            di_t subj = di_dict_pop(&e, str("subj"));
            assert(mark_last_access(&subj, varname));
            e = di_dict_set(e, str("subj"), subj);
        }
        e = di_dict_set(e, str("clauses"), cs);
    } else if (di_equal(op, str("clause"))) {
        // Case clause
        di_t body = di_dict_pop(&e, str("body"));
        if (!mark_last_access(&body, varname)) {
            di_t pats = di_dict_pop(&e, str("pats"));
            assert(mark_last_access_in_seq(&pats, varname));
            e = di_dict_set(e, str("pats"), pats);
        }
        e = di_dict_set(e, str("body"), body);
    } else if (di_equal(op, str("apply"))) {
        di_t args = di_dict_pop(&e, str("args"));
        if (!mark_last_access_in_seq(&args, varname)) {
            di_t func = di_dict_pop(&e, str("func"));
            assert(mark_last_access(&func, varname));
            e = di_dict_set(e, str("func"), func);
        }
        e = di_dict_set(e, str("args"), args);
    } else if (di_equal(op, str("array"))) {
        di_t elems = di_dict_pop(&e, str("elems"));
        assert(mark_last_access_in_seq(&elems, varname));
        e = di_dict_set(e, str("elems"), elems);
    } else if (di_equal(op, str("dict"))) {
        di_t entries = di_dict_pop(&e, str("entries"));
        assert(mark_last_access_in_seq(&entries, varname));
        e = di_dict_set(e, str("entries"), entries);
    } else if (di_equal(op, str("dictup"))) {
        di_t entries = di_dict_pop(&e, str("entries"));
        if (!mark_last_access_in_seq(&entries, varname)) {
            di_t subj = di_dict_pop(&e, str("subj"));
            assert(mark_last_access(&subj, varname));
            e = di_dict_set(e, str("subj"), subj);
        }
        e = di_dict_set(e, str("entries"), entries);
    } else if (di_equal(op, str("entry"))) {
        // Dict entry
        di_t value = di_dict_pop(&e, str("value"));
        if (!mark_last_access(&value, varname)) {
            di_t key = di_dict_pop(&e, str("key"));
            assert(mark_last_access(&key, varname));
            e = di_dict_set(e, str("key"), key);
        }
        e = di_dict_set(e, str("value"), value);
    } else if (di_equal(op, str("do"))) {
        di_t es = di_dict_pop(&e, str("seq"));
        assert(mark_last_access_in_seq(&es, varname));
        e = di_dict_set(e, str("seq"), es);
    } else {
        // This can't happen. We could assert(0) but we give an error message
        // for debugging.
        assert(di_is_string(op));
        assert(di_is_string(varname));
        error_expr_format(e, "Can't annotate %.*s as the last access of %.*s\n",
                          di_string_length(op),
                          di_string_chars(op),
                          di_string_length(varname),
                          di_string_chars(varname));
    }
    *e_ptr = e;
    return true;
}

/* Types:
 *
 * scope        = {name: null | deps, ...} (for variables null; for functions the deps)
 * nested scope = [scope, ...]
 */

// Initializes a scope to the function definitions on this level, so they can be
// used even if they're defined in the wrong order. We allow out-of-order
// function definitions, but not variable bindings.
di_t create_block_scope(di_t defs) {
    di_t new_scope = di_dict_empty();
    di_t name;
    for (int i = 0; (i = di_dict_iter(defs, i, &name, NULL)) != 0;) {
        new_scope = di_dict_set(new_scope, name, di_true());
    }
    return new_scope;
}

static di_t pattern(di_t e, di_t *scopes) {
    di_t op = di_dict_get(e, str("syntax"));
    if (di_equal(op, str("var"))) {
        di_t name = di_dict_get(e, str("name"));
        if (di_equal(name, str("_"))) {
            return e; // match-all, no variable is bound
        }
        di_t scope_value = lookup_nested_scope(name, scopes);
        di_t action;
        if (di_is_null(scope_value)) {
            di_t scope = di_array_shift(scopes);
            scope = di_dict_set(scope, name, di_true());
            di_array_unshift(scopes, scope);
            action = str("bind");
        } else if (!di_is_dict(scope_value)) {
            action = str("access");
        } else {
            // The variable is a function or closure. Supporting this case would
            // imply possibly instanciating the closure and accessing all its
            // captured variables here.
            error_expr_format(e, "Pattern matching on functions not supported");
        }
        e = di_dict_set(e, str("action"), action);
        di_t varset = di_dict_set(di_dict_empty(), name, action);
        e = di_dict_set(e, str("varset"), varset); // accessed or bound variables
    } else if (di_equal(op, str("lit"))) {
        return e;
    } else if (di_equal(op, str("regex"))) {
        //di_error("Regex not implemented.");
        return e; // TODO: Find out variable bindings in the pattern.
    } else if (di_equal(op, str("array"))) {
        di_t elems = di_dict_pop(&e, str("elems"));
        elems = patterns(elems, scopes);
        e = set_varset(e, get_varset(elems));
        e = di_dict_set(e, str("elems"), elems);
    } else if (di_equal(op, str("dict"))) {
        // entries = [{"syntax": "entry", "key":pattern, "value":expr}]
        di_t entries = di_dict_pop(&e, str("entries"));
        entries = dict_entries(entries, scopes, &pattern);
        e = set_varset(e, get_varset(entries));
        e = di_dict_set(e, str("entries"), entries);
    } else if (di_equal(op, str("dictup"))) {
        // subj{k: v, entries...}
        // subj = expr
        // entries = [{"syntax":"entry", "key":pattern, "value":pattern}, ...]
        di_t subj = di_dict_pop(&e, str("subj"));
        di_t entries = di_dict_pop(&e, str("entries"));
        subj = pattern(subj, scopes);
        entries = dict_entries(entries, scopes, &pattern);
        e = set_varset(e, varset_union(subj, entries));
        e = di_dict_set(e, str("subj"), subj);
        e = di_dict_set(e, str("entries"), entries);
    } else if (di_equal(op, str("@")) || di_equal(op, str("~"))) {
        di_t left = di_dict_pop(&e, str("left"));
        di_t right = di_dict_pop(&e, str("right"));
        left = pattern(left, scopes);
        right = pattern(right, scopes);
        e = set_varset(e, varset_union(left, right));
        e = di_dict_set(e, str("left"), left);
        e = di_dict_set(e, str("right"), right);
    } else {
        // error
        error_expr_format(e, "Invalid pattern "PRIstr, FMTstr(op));
    }
    return e;
}

// -------------------------------------------------------------------

// Set datatype using dicts with null values
// TODO: merge values "access" (+) "bind" => "access"
static di_t setunion(di_t a, di_t b) {
    if (di_is_null(a)) return b;
    if (di_is_null(b)) return a;
    di_size_t i = 0;
    di_t key;
    while ((i = di_dict_iter(b, i, &key, NULL)) != 0) {
        if (!di_dict_contains(a, key))
            a = di_dict_set(a, key, str("access"));
    }
    di_cleanup(b);
    return a;
}
/* static di_t setinter(di_t a, di_t b) { */
/*     di_size_t i = 0; */
/*     di_t key; */
/*     di_t c = di_dict_empty(); */
/*     while ((i = di_dict_iter(a, i, &key, NULL)) != 0) { */
/*         if (di_dict_contains(b, key)) */
/*             c = di_dict_set(c, key, di_null()); */
/*     } */
/*     di_cleanup(a); */
/*     di_cleanup(b); */
/*     return c; */
/* } */


// Returns the dict a minus the keys in b. Used when exiting a local scope b.
static di_t dict_diff(di_t a, di_t b) {
    if (di_is_null(a)) {
        // Nothing to do.
    } else {
        di_size_t i = 0;
        di_t key;
        while ((i = di_dict_iter(b, i, &key, NULL)) != 0) {
            if (di_dict_contains(a, key))
                a = di_dict_delete(a, key);
        }
    }
    di_cleanup(b);
    return a;
}

// Raises an error with the location of e and the message given by format and
// varargs.
static void error_expr_format(di_t e, const char *format, ...) {
    char buf[256] = {0};
    int line = di_to_int(di_dict_get(e, str("line"))),
        col  = di_to_int(di_dict_get(e, str("column")));
    int written1 = snprintf(buf, sizeof(buf), "%d:%d: ", line, col);
    assert(written1 > 0);
    va_list va;
    va_start(va, format);
    int written2 = vsnprintf(buf + written1, sizeof(buf) - written1, format, va);
    va_end(va);
    assert(written2 > 0);
    di_error(str(buf));
}
