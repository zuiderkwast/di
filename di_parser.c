#include <stdarg.h>
#include <stdio.h>
#include "di.h"
#include "di_lexer.h"
#include "di_parser.h"

/*----------------------------------------------------------------------------
 * Parsers for some of the main levels in the syntax
 *----------------------------------------------------------------------------*/

di_t di_parse(di_t source);

// Forward declarations
static di_t create_parser(di_t source);
static di_t block(di_t *p, int l, int c);
static di_t expr(di_t *p);
static void validate_expr(di_t e);
static void validate_pattern(di_t e);
static void validate_array(di_t es, void (*validator)(di_t));

/* Parses source code and returns parse tree. The root node is an array of
 * expressions. */
di_t di_parse(di_t source) {
    di_t p = create_parser(source);
    di_t ast = block(&p, 1, 1);
    di_cleanup(p);
    return ast;
}

/*----------------------------------------------------------------------------
 * Helpers for setting parser flags, raising errors, fetching tokens, etc.
 *----------------------------------------------------------------------------*/

static inline di_t str(const char *chars) {
    return di_string_from_cstring(chars);
}

/* Set to true when expecting a pattern and false when expecting expr. */
static inline void error(di_t message, int line, int col) {
    fprintf(stderr, "Parse error on line %d, column %d: %.*s\n",
            line,
            col,
            di_string_length(message),
            di_string_chars(message));
    exit(-1);
}

// fetches a new current token to the parser state.
static void fetch_next_token(di_t *p) {
    di_t token_str = str("token");
    di_t old_token = di_dict_get(*p, token_str);
    di_t lexer_str = str("lexer");
    di_t lexer = di_dict_get(*p, lexer_str);
    di_t token = di_lex(&lexer, old_token);
    *p = di_dict_set(*p, lexer_str, lexer);
    *p = di_dict_set(*p, token_str, token);
}

static di_t create_parser(di_t source) {
    di_t lexer = di_lexer_create(source);
    di_t p = di_dict_empty();
    p = di_dict_set(p, str("lexer"), lexer);
    p = di_dict_set(p, str("token"), di_null());
    fetch_next_token(&p);
    return p;
}

// Returns the op of the current token and sets line and col.
static di_t get_token_op(const di_t *p, int *line, int *col) {
    di_t token = di_dict_get(*p, str("token"));
    *line = di_to_int(di_dict_get(token, str("line")));
    *col  = di_to_int(di_dict_get(token, str("column")));
    return  di_dict_get(token, str("op"));
}

static bool is_token(const di_t *p, const char *token_op) {
    di_t expect = str(token_op);
    di_t token = di_dict_get(*p, str("token"));
    di_t op = di_dict_get(token, str("op"));
    bool ok = di_equal(expect, op);
    di_cleanup(expect);
    return ok;
}

// NULL-terminated args
static di_t mkdict(const char *k1, di_t v1, ...) {
    va_list va;
    char *key;
    di_t dict = di_dict_empty();
    dict = di_dict_set(dict, str(k1), v1);
    va_start(va, v1);
    while ((key = va_arg(va, char*)) != NULL) {
        di_t value = va_arg(va, di_t);
        dict = di_dict_set(dict, str(key), value);
    }
    va_end(va);
    return dict;
}

// Returns the data of the current token in the parser state.
static di_t get_token_data(di_t *p) {
    di_t token = di_dict_get(*p, str("token"));
    di_t data  = di_dict_get(token, str("data"));
    return data;
}

// Sets the position of the current token in the parser state.
static void copy_token_pos(di_t *p, int *line, int *col) {
    if (!line && !col) return;
    di_t token = di_dict_get(*p, str("token"));
    if (line) {
        di_t l = di_dict_get(token, str("line"));
        *line = di_to_int(l);
    }
    if (col) {
        di_t c = di_dict_get(token, str("column"));
        *col  = di_to_int(c);
    }
}

// if token matches, sets line and col, discards the token and returns true.
// otherwise returns false and leaves line, col and the current token in the
// parser state unchanged.
static bool try_token(di_t *p, int *line, int *col, const char *token_op) {
    bool ok = is_token(p, token_op);
    if (ok) {
        copy_token_pos(p, line, col);
        fetch_next_token(p);
    }
    return ok;
}

// consumes a token and asserts that its op is token_op.
static void eat(di_t *p, const char *token_op) {
    di_t expect = str(token_op);
    int l, c; // line and col
    di_t op = get_token_op(p, &l, &c);
    bool ok = di_equal(expect, op);
    di_cleanup(expect);
    if (ok) {
        fetch_next_token(p);
        return;
    }
    di_t message = str("Unexpected ");
    message = di_string_concat(message, op);
    message = di_string_concat(message, str(". Expecting "));
    message = di_string_concat(message, expect);
    message = di_string_concat(message, str("."));
    error(message, l, c);
}

static void error_unexpected_token(di_t *p) {
    int l, c; // line and col
    di_t op = get_token_op(p, &l, &c);
    di_t message = str("Unexpected ");
    message = di_string_concat(message, op);
    error(message, l, c);
}

/*----------------------------------------------------------------------------
 * Expressions. expr() and friends take a parser pointer and return a dict on
 * the form {"syntax": WHAT, "line": N, "column": M, ...} where the rest depends
 * on WHAT.
 *----------------------------------------------------------------------------*/

static di_t vmknode(const char *tagk, const char *tagv, int line, int col,
                    va_list va) {
    char *k;
    di_t dict = di_dict_empty();
    dict = di_dict_set(dict, str(tagk), str(tagv));
    dict = di_dict_set(dict, str("line"), di_from_int(line));
    dict = di_dict_set(dict, str("column"), di_from_int(col));
    while ((k = va_arg(va, char*)) != NULL) {
        di_t key = str(k);
        di_t value = va_arg(va, di_t);
        dict = di_dict_set(dict, key, value);
    }
    return dict;
}

// Makes an expr dict. Additional args are pairs of (char* key, di_t value),
// terminated by a NULL arg.
static di_t mkexpr(const char *op, int line, int col, ...) {
    va_list va;
    va_start(va, col);
    di_t node = vmknode("syntax", op, line, col, va);
    va_end(va);
    return node;
}

// [{"syntax": "clause", "pats": [pattern], "body": expr}, ...]
static di_t case_clauses(di_t *p) {
    int l, c;
    di_t clauses = di_array_empty();
    do {
        di_t pat = expr(p);
        validate_pattern(pat);
        eat(p, "->");
        di_t exp = expr(p);
        validate_expr(exp);
        di_t pats = di_array_empty();
        di_array_push(&pats, pat); // for 'case', pats is a singleton array.
        di_t clause = mkdict("syntax", str("clause"), "pats", pats, "body", exp,
                             NULL);
        di_array_push(&clauses, clause);
    } while (try_token(p, &l, &c, ";"));
    eat(p, "end");
    return clauses;
}

static bool is_func_def(di_t e) {
    di_t op = di_dict_get(e, str("syntax"));
    if (!di_equal(op, str("="))) return false;
    di_t lhs = di_dict_get(e, str("left"));
    if (!di_equal(di_dict_get(lhs, str("syntax")), str("apply"))) return false;
    return true;
}

// Validates and adds a function clause to the dict of function definitions with
// the function names as the keys. Pre-condition: is_func_def(e) is true.
static di_t add_func_def_clause(di_t funcdefs, di_t e) {
    di_t lhs = di_dict_get(e, str("left"));     // "="
    di_t func = di_dict_get(lhs, str("func"));  // "apply"
    if (!di_equal(di_dict_get(func, str("syntax")), str("var"))) {
        error(str("Invalid function name."),
              di_to_int(di_dict_get(e, str("line"))),
              di_to_int(di_dict_get(e, str("column"))));
    }
    di_t name = di_dict_get(func, str("name")); // "var"
    di_t params = di_dict_get(lhs, str("args"));
    validate_array(params, &validate_pattern);
    di_t arity = di_from_int(di_array_length(params));
    di_t rhs = di_dict_get(e, str("right"));
    validate_expr(rhs);
    // Lookup or create function definition entry and list of function clauses.
    di_t def = di_dict_get(funcdefs, name);
    di_t clauses;
    if (di_is_null(def)) {
        def = di_dict_empty();
        def = di_dict_set(def, str("name"), name);
        def = di_dict_set(def, str("arity"), arity);
        clauses = di_array_empty();
    } else {
        if (!di_equal(arity, di_dict_get(def, str("arity")))) {
            error(str("Arity mismatches previous clauses."),
                  di_to_int(di_dict_get(e, str("line"))),
                  di_to_int(di_dict_get(e, str("column"))));
        }
        name = di_dict_get(def, str("name")); // use this allocation of name
        clauses = di_dict_get(def, str("clauses"));
    }
    // Turn the lhs (expr: apply) into a clause. In this way, we keep its line
    // and column. Rename args to pats, add body, delete func.
    lhs = di_dict_set(lhs, str("syntax"), str("clause"));
    lhs = di_dict_delete(lhs, str("func"));
    lhs = di_dict_set(lhs, str("pats"), params);
    lhs = di_dict_delete(lhs, str("args"));
    lhs = di_dict_set(lhs, str("body"), rhs);
    di_array_push(&clauses, lhs);
    def = di_dict_set(def, str("clauses"), clauses);
    funcdefs = di_dict_set(funcdefs, name, def);
    di_cleanup(e);
    return funcdefs;
}

// Body of a `do expr ; ... end` construct. Expressions and function definitions
// are partitioned.
static di_t block(di_t *p, int l, int c) {
    di_t es = di_array_empty();
    di_t fs = di_dict_empty();
    do {
        di_t e = expr(p);
        di_t op = di_dict_get(e, str("syntax"));
        if (is_func_def(e)) {
            fs = add_func_def_clause(fs, e);
        } else {
            // "=" is not allowed in ordinary expressions
            if (di_equal(op, str("="))) {
                validate_pattern(di_dict_get(e, str("left")));
                validate_expr(di_dict_get(e, str("right")));
            } else {
                validate_expr(e);
            }
            di_array_push(&es, e);
        }
    } while (try_token(p, NULL, NULL, ";"));
    eat(p, "end");
    return mkexpr("do", l, c, "seq", es, "defs", fs, NULL);
}

// Creates a binary expression {"syntax": op, "left": left, "right": right}.
// Copies line and column from the left operand. Shall we use the op's location
// instead?
static di_t mkbinopexpr(const char *op_str, di_t left, di_t right) {
    di_t op = str(op_str);
    di_t line = di_dict_get(left, str("line"));
    di_t col  = di_dict_get(left, str("column"));
    return mkdict("syntax", op, "line", line, "column", col, "left", left,
                  "right", right, NULL);
}

// Parses a sequence of nextexpr nodes separated by any of the supplied tokens,
// passed as an di_t array of di_t strings
// expr -> nextexpr (binop nextexpr)*
static di_t leftassoc_expr(di_t *p, di_t (*nextexpr)(di_t *p), ...) {
    va_list va;
    di_t e1 = nextexpr(p);
    char const *arg;
    do {
        int l, c;
        va_start(va, nextexpr);
        while ((arg = va_arg(va, char const *)) != NULL) {
            if (try_token(p, &l, &c, arg)) {
                di_t e2 = nextexpr(p);
                e1 = mkbinopexpr(arg, e1, e2);
                break;
            }
        }
        va_end(va);
    } while (arg != NULL); // NULL-arg means no binop token matched
    return e1;
}

static di_t expr0(di_t *p);
static di_t expr1(di_t *p);
static di_t expr2(di_t *p);
static di_t expr3(di_t *p);
static di_t expr4(di_t *p);
static di_t expr5(di_t *p);

static di_t expr(di_t *p) {
    // "=" is right associative
    di_t e0 = expr0(p);
    if (try_token(p, NULL, NULL, "=")) {
        di_t e = expr(p);
        return mkbinopexpr("=", e0, e);
    }
    return e0;
}

static di_t expr0(di_t *p) {
    return leftassoc_expr(p, expr1, "and", "or", NULL);
}

static di_t expr1(di_t *p) {
    return leftassoc_expr(p, expr2, "<", ">", "=<", ">=", "==", "!=", NULL);
}

static di_t expr2(di_t *p) {
    return leftassoc_expr(p, expr3, "+", "-", "~", "@", NULL);
}

static di_t expr3(di_t *p) {
    return leftassoc_expr(p, expr4, "*", "/", "div", "mod", NULL);
}

// expr -> expr '(' arg, arg, ... ')' (function application)
// expr -> expr '{' key: val, ... '}' (dict update)
static di_t expr4(di_t *p) {
    di_t e = expr5(p);
    int l, c;
    while (1) {
        if (try_token(p, &l, &c, "(")) {
            // Function application f(x,y). The LHS of a function definition is
            // identical and is rewritten later.
            di_t es = di_array_empty(); // args
            if (try_token(p, NULL, NULL, ")")) {
                // empty arg list
            } else {
                do {
                    di_t e = expr(p);
                    di_array_push(&es, e);
                } while (try_token(p, NULL, NULL, ","));
                eat(p, ")");
            }
            e = mkexpr("apply", l, c, "func", e, "args", es, NULL);
        } else if (try_token(p, &l, &c, "{")) {
            // dict update d{k: v}
            di_t entries = di_array_empty();
            if (try_token(p, NULL, NULL, "}")) {
                // empty key-value list
            } else {
                do {
                    di_t k = expr(p);
                    eat(p, ":");
                    di_t v = expr(p);
                    di_t entry = mkdict("syntax", str("entry"),
                                       "key", k, "value", v, NULL);
                    di_array_push(&entries, entry);
                } while (try_token(p, NULL, NULL, ","));
                eat(p, "}");
            }
            e = mkexpr("dictup", l, c, "subj", e, "entries", entries, NULL);
        } else {
            break;
        }
    }
    return e;
}

static di_t expr5(di_t *p) {
    int l, c; // line and col
    if (try_token(p, &l, &c, "case")) {
        di_t subj = expr(p);
        validate_expr(subj);
        eat(p, "of");
        di_t clauses = case_clauses(p);
        return mkexpr("case", l, c, "subj", subj, "clauses", clauses, NULL);
    } else if (try_token(p, &l, &c, "do")) {
        return block(p, l, c);
    } else if (try_token(p, &l, &c, "if")) {
        di_t cond = expr(p);
        validate_expr(cond);
        eat(p, "then");
        di_t if_then = expr(p);
        validate_expr(if_then);
        try_token(p, NULL, NULL, ";"); // Optional ";"
        // TODO: Make else optional and default to null like in Clojure?
        eat(p, "else");
        di_t if_else = expr(p);
        validate_expr(if_else);
        return mkexpr("if", l, c, "cond", cond, "then", if_then, "else",
                      if_else, NULL);
    } else if (0) {
        // TODO: "let-in", "where"
        // TODO: "lambda"
    } else if (try_token(p, &l, &c, "[")) {
        di_t elems = di_array_empty();
        if (try_token(p, NULL, NULL, "]")) {
            // empty array
        } else {
            do {
                di_t elem = expr(p);
                di_array_push(&elems, elem);
            } while (try_token(p, NULL, NULL, ","));
            eat(p, "]");
        }
        return mkexpr("array", l, c, "elems", elems, NULL);
    } else if (try_token(p, &l, &c, "{")) {
        // Dictionary constructor
        di_t entries = di_array_empty();
        if (try_token(p, NULL, NULL, "}")) {
            // empty dict
        } else {
            do {
                di_t k = expr(p);
                eat(p, ":");
                di_t v = expr(p);
                di_t entry = mkdict("syntax", str("entry"),
                                   "key", k, "value", v, NULL);
                di_array_push(&entries, entry);
            } while (try_token(p, NULL, NULL, ","));
            eat(p, "}");
        }
        return mkexpr("dict", l, c, "entries", entries, NULL);
    } else if (is_token(p, "ident")) {
        // variable
        di_t name = get_token_data(p);
        copy_token_pos(p, &l, &c);
        di_t e = mkexpr("var", l, c, "name", name, NULL);
        fetch_next_token(p);
        return e;
    } else if (is_token(p, "lit")) {
        // Literal
        di_t value = get_token_data(p);
        copy_token_pos(p, &l, &c);
        di_t e = mkexpr("lit", l, c, "value", value, NULL);
        fetch_next_token(p);
        return e;
    } else if (is_token(p, "regex")) {
        di_t re = get_token_data(p);
        copy_token_pos(p, &l, &c);
        di_t e = mkexpr("regex", l, c, "regex", re, NULL);
        fetch_next_token(p);
        return e;
    } else if (try_token(p, &l, &c, "-")) {
        di_t e = expr(p);
        return mkexpr("-", l, c, "right", e, NULL);
    } else if (try_token(p, &l, &c, "not")) {
        di_t e = expr(p);
        return mkexpr("not", l, c, "right", e, NULL);
    } else if (try_token(p, NULL, NULL, "(")) {
        di_t e = expr(p);
        eat(p, ")");
        return e;
    } else {
        // Fail.
        error_unexpected_token(p);
    }
    // never reached
    return di_null();
}

static void error_context(di_t e, di_t ctx) {
    di_t op = di_dict_get(e, str("syntax"));
    int l = di_to_int(di_dict_get(e, str("line")));
    int c = di_to_int(di_dict_get(e, str("column")));
    di_t message = str("Unexpected ");
    message = di_string_concat(message, op);
    message = di_string_concat(message, str(" in "));
    message = di_string_concat(message, ctx);
    message = di_string_concat(message, str(" context."));
    error(message, l, c);
}

// Validates the subexpressions or subpatterns of e using the provided validator
// function. Only syntactic elements that can appear both as expressions and
// patterns are handled.
static void validate_array(di_t es, void (*validator)(di_t)) {
    di_size_t n = di_array_length(es);
    for (di_size_t i = 0; i < n; i++) {
        validator(di_array_get(es, i));
    }
}
static void validate_entries(di_t entries, void (*validator)(di_t)) {
    di_size_t n = di_array_length(entries);
    for (di_size_t i = 0; i < n; i++) {
        di_t entry = di_array_get(entries, i);
        assert(di_equal(di_dict_get(entry, str("syntax")), str("entry")));
        validator(di_dict_get(entry, str("key")));
        validator(di_dict_get(entry, str("value")));
    }
}
// Calls the validator function on the subexpressions of e. Validator is either
// validate_expr or validate_pattern.
static void validate_children(di_t e, void (*validator)(di_t)) {
    di_t op = di_dict_get(e, str("op"));
    const char *binops[] = {
        "and", "or", "not",
        "<", ">", "=<", ">=", "==", "!=",
        "+", "-", "*", "/", "div", "mod",
        "~", "@", "="
    };
    for (int i = 0; i < sizeof(binops) / sizeof(*binops); i++) {
        if (di_equal(op, str(binops[i]))) {
            di_t left = di_dict_get(e, str("left"));
            if (!di_is_null(left))
                validator(left);
            validator(di_dict_get(e, str("right")));
            return;
        }
    }
    if (di_equal(op, str("apply"))) {
        // Only expr, but the parser doesn't forbid patterns earlier because
        // function definitions are parsed as apply on the LHS (where the params
        // are patterns) and converted from apply to function definitions later.
        validator(di_dict_get(e, str("func")));
        validate_array(di_dict_get(e, str("args")), validator);
    }
    if (di_equal(op, str("array"))) {
        validate_array(di_dict_get(e, str("elems")), validator);
        return;
    }
    if (di_equal(op, str("dict"))) {
        validate_entries(di_dict_get(e, str("entries")), validator);
    }
    if (di_equal(op, str("dictup"))) {
        validator(di_dict_get(e, str("subj")));
        validate_entries(di_dict_get(e, str("entries")), validator);
    }
}
static void validate_expr(di_t e) {
    di_t op = di_dict_get(e, str("syntax"));
    if (di_equal(op, str("=")) || di_equal(op, str("regex"))) {
        error_context(e, str("expression"));
    }
    validate_children(e, &validate_expr);
}
static void validate_pattern(di_t e) {
    // "=", "~", "@", array, dict, var, literal and regex are valid.
    const char *invalids[] = {
        "do", "if", "case", "apply",
        "and", "or", "not",
        "<", ">", "=<", ">=", "==", "!=",
        "+", "-", "*", "/", "div", "mod"
    };
    di_t op = di_dict_get(e, str("syntax"));
    for (int i = 0; i < sizeof(invalids) / sizeof(*invalids); i++) {
        di_t invalid = str(invalids[i]);
        if (di_equal(op, invalid)) {
            error_context(e, str("pattern"));
        }
    }
    validate_children(e, &validate_pattern);
}
