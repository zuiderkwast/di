#include <stdarg.h>
#include "di.h"
#include "di_lexer.h"
#include "di_parser.h"

/*----------------------------------------------------------------------------
 * Parsers for some of the main levels in the syntax
 *----------------------------------------------------------------------------*/

static di_t create_parser(di_t source);
static di_t expr_seq(di_t *p);
static di_t expr(di_t *p);

/* Parses source code and returns parse tree. The root node is an array of
 * expressions. */
di_t di_parse(di_t source) {
    di_t p = create_parser(source);
    di_t exprs = expr_seq(&p);
    di_cleanup(p);
    return exprs;
}

/*----------------------------------------------------------------------------
 * Helpers for setting parser flags, raising errors, fetching tokens, etc.
 *----------------------------------------------------------------------------*/

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
    di_t token_str = di_string_from_cstring("token");
    di_t old_token = di_dict_get(*p, token_str);
    di_t lexer_str = di_string_from_cstring("lexer");
    di_t lexer = di_dict_get(*p, lexer_str);
    di_t token = di_lex(&lexer, old_token);
    *p = di_dict_set(*p, lexer_str, lexer);
    *p = di_dict_set(*p, token_str, token);
}

static di_t create_parser(di_t source) {
    di_t lexer = di_lexer_create(source);
    di_t p = di_dict_empty();
    p = di_dict_set(p, di_string_from_cstring("lexer"), lexer);
    p = di_dict_set(p, di_string_from_cstring("token"), di_null());
    fetch_next_token(&p);
    return p;
}

// Returns the op of the current token and sets line and col.
static di_t get_token_op(const di_t *p, int *line, int *col) {
    di_t token = di_dict_get(*p, di_string_from_cstring("token"));
    *line = di_to_int(di_dict_get(token, di_string_from_cstring("line")));
    *col  = di_to_int(di_dict_get(token, di_string_from_cstring("column")));
    return  di_dict_get(token, di_string_from_cstring("op"));
}

static bool is_token(const di_t *p, const char *token_op) {
    di_t expect = di_string_from_cstring(token_op);
    di_t token = di_dict_get(*p, di_string_from_cstring("token"));
    di_t op = di_dict_get(token, di_string_from_cstring("op"));
    bool ok = di_equal(expect, op);
    di_cleanup(expect);
    return ok;
}

// NULL-terminated args
static di_t mkdict(const char *k1, di_t v1, ...) {
    va_list va;
    char *key;
    di_t dict = di_dict_empty();
    dict = di_dict_set(dict, di_string_from_cstring(k1), v1);
    va_start(va, v1);
    while ((key = va_arg(va, char*)) != NULL) {
        di_t value = va_arg(va, di_t);
        dict = di_dict_set(dict, di_string_from_cstring(key), value);
    }
    va_end(va);
    return dict;
}

// Returns the data of the current token in the parser state.
static di_t get_token_data(di_t *p) {
    di_t token = di_dict_get(*p, di_string_from_cstring("token"));
    di_t data  = di_dict_get(token, di_string_from_cstring("data"));
    return data;
}

// Sets the position of the current token in the parser state.
static void copy_token_pos(di_t *p, int *line, int *col) {
    if (!line && !col) return;
    di_t token = di_dict_get(*p, di_string_from_cstring("token"));
    if (line) {
        di_t l = di_dict_get(token, di_string_from_cstring("line"));
        *line = di_to_int(l);
    }
    if (col) {
        di_t c = di_dict_get(token, di_string_from_cstring("column"));
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
    di_t expect = di_string_from_cstring(token_op);
    int l, c; // line and col
    di_t op = get_token_op(p, &l, &c);
    bool ok = di_equal(expect, op);
    di_cleanup(expect);
    if (ok) {
        fetch_next_token(p);
        return;
    }
    di_t message = di_string_from_cstring("Unexpected ");
    message = di_string_concat(message, op);
    message = di_string_concat(message,
                               di_string_from_cstring(". Expecting "));
    message = di_string_concat(message, expect);
    message = di_string_concat(message, di_string_from_cstring("."));
    error(message, l, c);
}

static void error_unexpected_token(di_t *p, char * const rule) {
    int l, c; // line and col
    di_t op = get_token_op(p, &l, &c);
    di_t message = di_string_from_cstring("Unexpected ");
    message = di_string_concat(message, op);
    message = di_string_concat(message, di_string_from_cstring(", parsing "));
    message = di_string_concat(message, di_string_from_cstring(rule));
    error(message, l, c);
}

/*----------------------------------------------------------------------------
 * Expressions. expr() and friends take a parser pointer and return a dict on
 * the form {"expr": WHAT, "line": N, "column": M, ...} where the rest depends
 * on WHAT.
 *----------------------------------------------------------------------------*/

static di_t vmknode(const char *tagk, const char *tagv, int line, int col,
                    va_list va) {
    char *k;
    di_t dict = di_dict_empty();
    dict = di_dict_set(dict, di_string_from_cstring(tagk),
                       di_string_from_cstring(tagv));
    dict = di_dict_set(dict, di_string_from_cstring("line"),
                       di_from_int(line));
    dict = di_dict_set(dict, di_string_from_cstring("column"),
                       di_from_int(col));
    while ((k = va_arg(va, char*)) != NULL) {
        di_t key = di_string_from_cstring(k);
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
    di_t node = vmknode("expr", op, line, col, va);
    va_end(va);
    return node;
}

// [{"pat": pattern, "expr": expr}, ...]
static di_t case_alts(di_t *p) {
    int l, c;
    di_t alts = di_array_empty();
    do {
        di_t pat = expr(p);
        eat(p, "->");
        di_t exp = expr(p);
        // TODO: validate pat and exp as pattern and expression, resp.
        di_t alt = mkdict("pat", pat, "expr", exp, NULL);
        di_array_push(&alts, alt);
    } while (try_token(p, &l, &c, ";"));
    eat(p, "end");
    return alts;
}

// Body of a `do expr ; ... end` construct.
static di_t expr_seq(di_t *p) {
    int l, c;
    di_t es = di_array_empty();
    do {
        di_t e = expr(p);
        di_array_push(&es, e);
    } while (try_token(p, &l, &c, ";"));
    eat(p, "end");
    // TODO: Combine function definitions with multiple clauses.
    return es;
}

// Creates a binary expression {"expr": op, "left": left, "right": right}.
// Copies line and column from the left operand. Shall we use the op's location
// instead?
static di_t mkbinopexpr(const char *op_str, di_t left, di_t right) {
    di_t op = di_string_from_cstring(op_str);
    di_t line = di_dict_get(left, di_string_from_cstring("line"));
    di_t col  = di_dict_get(left, di_string_from_cstring("column"));
    return mkdict("expr", op, "line", line, "column", col, "left", left,
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
    // FIXME: "=" is right associative
    //return leftassoc_expr(p, expr0, "=", NULL);
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
    return leftassoc_expr(p, expr4, "*", "/", "mod", NULL);
}

// expr -> expr '(' arg, arg, ... ')' (function application)
// expr -> expr '{' key: val, ... '}' (dict update)
static di_t expr4(di_t *p) {
    di_t e = expr5(p);
    int l, c;
    while (1) {
        if (try_token(p, &l, &c, "(")) {
            // function application f(x,y)
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
            di_t pairs = di_array_empty();
            if (try_token(p, NULL, NULL, "}")) {
                // empty key-value list
            } else {
                do {
                    di_t k = expr(p);
                    eat(p, ":");
                    di_t v = expr(p);
                    di_t pair = mkdict("key", k, "value", v, NULL);
                    di_array_push(&pairs, pair);
                } while (try_token(p, NULL, NULL, ","));
                eat(p, "}");
            }
            e = mkexpr("dictup", l, c, "subj", e, "pairs", pairs, NULL);
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
        eat(p, "of");
        di_t alts = case_alts(p);
        return mkexpr("case", l, c, "subj", subj, "alts", alts, NULL);
    } else if (try_token(p, &l, &c, "do")) {
        di_t seq = expr_seq(p);
        return mkexpr("do", l, c, "seq", seq, NULL);
    } else if (try_token(p, &l, &c, "if")) {
        di_t cond = expr(p);
        eat(p, "then");
        di_t if_then = expr(p);
        try_token(p, NULL, NULL, ";"); // Optional ";"
        eat(p, "else");
        di_t if_else = expr(p);
        return mkexpr("if", l, c, "cond", cond, "then", if_then, "else",
                      if_else, NULL);
    } else if (0) {
        // TODO: "let-in", "where"
        // TODO: "fun", "lambda", etc.
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
        di_t pairs = di_array_empty();
        if (try_token(p, NULL, NULL, "}")) {
            // empty dict
        } else {
            do {
                di_t k = expr(p);
                eat(p, ":");
                di_t v = expr(p);
                di_t pair = mkdict("key", k, "value", v, NULL);
                di_array_push(&pairs, pair);
            } while (try_token(p, NULL, NULL, ","));
            eat(p, "}");
        }
        return mkexpr("dict", l, c, "pairs", pairs, NULL);
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
        error_unexpected_token(p, "expr");
    }
    // never reached
    return di_null();
}
