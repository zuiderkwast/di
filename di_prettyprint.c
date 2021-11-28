#include <stdio.h>
#include <stdbool.h>
#include "di.h"
#include "di_prettyprint.h"

#define STEP 2 /* indentation per level */

/* Value to source code. Does not free value. */
di_t di_to_source(di_t value, int indent) {
    if (di_is_int(value)) {
        char buf[32];
        snprintf(buf, 32, "%d", di_to_int(value));
        return di_string_from_cstring(buf);
    } else if (di_is_double(value)) {
        char buf[50];
        snprintf(buf, 50, "%f", di_to_double(value));
        return di_string_from_cstring(buf);
    } else if (di_is_string(value)) {
        char *chars = di_string_chars(value);
        di_size_t length = di_string_length(value);
        // compute literal length;
        di_size_t len = 2; // enclosing quotes
        for (di_size_t i = 0; i < length; i++) {
            char c = chars[i];
            // Escapes: \" \\ \/ \b \f \n \r \t \uHHHH
            if (c == '"' || c == '\\' || c == '/' || c == '\b' || c == '\f' ||
                c == '\n' || c == '\r' || c == '\t') len += 2;
            else len++;
        }
        di_t lit_str = di_string_create_presized(len);
        char *lit = di_string_chars(lit_str);
        lit[0] = lit[len - 1] = '"'; // enclosing quotes
        di_size_t j = 1;             // position in lit
        for (di_size_t i = 0; i < length; i++) {
            char c = chars[i];
            // Escapes: \" \\ \/ \b \f \n \r \t \uHHHH.
            // We don't generate \uHHHH escapes. Plain UTF-8 is fine.
            if (c == '"' || c == '\\' || c == '/' || c == '\b' || c == '\f' ||
                c == '\n' || c == '\r' || c == '\t') {
                lit[j++] = '\\';
                switch (chars[i]) {
                case '\b': lit[j++] = 'b'; break;
                case '\f': lit[j++] = 'f'; break;
                case '\n': lit[j++] = 'n'; break;
                case '\r': lit[j++] = 'r'; break;
                case '\t': lit[j++] = 't'; break;
                default: lit[j++] = chars[i];
                }
            } else {
                lit[j++] = chars[i];
            }
        }
        return lit_str;
    } else if (di_is_null(value)) {
        return di_string_from_cstring("null");
    } else if (di_is_false(value)) {
        return di_string_from_cstring("false");
    } else if (di_is_true(value)) {
        return di_string_from_cstring("true");
    } else if (di_is_array(value)) {
        int n = di_array_length(value);
        if (n == 0)
            return di_string_from_cstring("[]");
        di_t str = di_string_from_cstring("[\n");
        char buf[80];
        snprintf(buf, 80, "%*s", indent + STEP, "");
        for (int i = 0; i < n; i++) {
            str = di_string_append_chars(str, buf, strlen(buf));
            di_t elem = di_array_get(value, i);
            str = di_string_concat(str, di_to_source(elem, indent + STEP));
            if (i < n - 1)
                str = di_string_append_chars(str, ",", 1);
            str = di_string_append_chars(str, "\n", 1);
        }
        snprintf(buf, 80, "%*s]", indent, "");
        str = di_string_append_chars(str, buf, strlen(buf));
        return str;
    } else if (di_is_dict(value)) {
        di_size_t n = di_dict_size(value);
        if (n == 0)
            return di_string_from_cstring("{}");
        di_t str = di_string_from_cstring("{\n");
        di_size_t cursor = 0, i = 0;
        di_t k, v;
        char buf[80];
        snprintf(buf, 80, "%*s", indent + STEP, "");
        while ((cursor = di_dict_iter(value, cursor, &k, &v)) != 0) {
            str = di_string_append_chars(str, buf, strlen(buf));
            di_t keystr = di_to_source(k, indent + STEP);
            str = di_string_concat(str, keystr);
            str = di_string_append_chars(str, ": ", 2);
            str = di_string_concat(str, di_to_source(v, indent + STEP));
            if (i++ < n - 1)
                str = di_string_append_chars(str, ",", 1);
            str = di_string_append_chars(str, "\n", 1);
        }
        snprintf(buf, 80, "%*s}", indent, "");
        str = di_string_append_chars(str, buf, strlen(buf));
        return str;
    } else if (di_is_undefined(value)) {
        return di_string_from_cstring("(undefined)");
    } else if (di_is_deleted(value)) {
        return di_string_from_cstring("(deleted)");
    } else if (di_is_empty(value)) {
        return di_string_from_cstring("(empty)");
    }
    //assert(0); // not implemented for any other types
    return di_null();
}

static inline di_t pp_literal(di_t value) {
    return di_to_source(value, 0);
}

// just a shorter name for di_string_from_cstring
static inline di_t s(char * const cstring) {
    return di_string_from_cstring(cstring);
}

// prints a di string.
static inline void ps(di_t s) {
    printf("%.*s", di_string_length(s), di_string_chars(s));
}

// true iff op is a binary operator.
static inline bool is_binop(di_t pp, di_t op) {
    di_t binops = di_dict_get(pp, s("binops"));
    int n = di_array_length(binops);
    int i;
    for (i = 0; i < n; i++) {
        di_t elem = di_array_get(binops, i);
        if (di_equal(op, elem))
            return true;
    }
    return false;
}

// creates a pretty-printer state
static di_t create_pp(void) {
    di_t pp = di_dict_empty();
    di_t binops = di_array_empty();
    char * ops[] = {"and", "or", "<", ">", "≤", "≥", "≠", "==",
        "=",
        "!=", "@", "~", "+", "-", "*", "/", "mod"};
    int i;
    for (i = 0; i < sizeof(ops) / sizeof(char *); i++) {
        di_array_push(&binops, s(ops[i]));
    }
    pp = di_dict_set(pp, s("binops"), binops);
    return pp;
}

static void expr(di_t pp, di_t e, int indent);

static void exprs(di_t pp, di_t es, int indent) {
    di_size_t n = di_array_length(es);
    for (di_size_t i = 0; i < n; i++) {
        if (i > 0) printf("\n");
        if (indent > 0)
            printf("%*s", indent, "");
        di_t e = di_array_get(es, i);
        expr(pp, e, indent);
    }
}

static void expr(di_t pp, di_t e, int indent) {
    di_t op = di_dict_get(e, s("expr"));
    if (di_equal(op, s("lit"))) {
        di_t value = di_dict_get(e, s("value"));
        di_t src = pp_literal(value);
        ps(src);
        di_cleanup(src);
    } else if (di_equal(op, s("var"))) {
        di_t name = di_dict_get(e, s("name"));
        ps(name);
        di_cleanup(name);
    } else if (di_equal(op, s("regex"))) {
        di_t regex = di_dict_get(e, s("regex"));
        printf("/");
        ps(regex);
        di_cleanup(regex);
        printf("/");
    } else if (di_equal(op, s("array"))) {
        di_t elems = di_dict_get(e, s("elems"));
        int n = di_array_length(elems);
        if (n == 0) {
            printf("[]");
        } else {
            printf("[");
            for (int i = 0; i < n; i++) {
                expr(pp, di_array_get(elems, i), indent + 1);
                if (i < n - 1)
                    printf(",\n%*s", indent + 1, "");
            }
            printf("]");
        }
    } else if (di_equal(op, s("dict"))) {
        di_t pairs = di_dict_get(e, s("pairs"));
        int n = di_array_length(pairs);
        if (n == 0) {
            printf("{}");
        } else {
            printf("{");
            for (int i = 0; i < n; i++) {
                di_t pair = di_array_get(pairs, i);
                di_t k = di_dict_get(pair, s("key"));
                di_t v = di_dict_get(pair, s("value"));
                expr(pp, k, indent + 1);
                printf(": ");
                expr(pp, v, indent + 1);
                if (i < n - 1)
                    printf(",\n%*s", indent + 1, "");
            }
            printf("}");
        }
    } else if (di_equal(op, s("apply"))) {
        di_t func = di_dict_get(e, s("func"));
        di_t args = di_dict_get(e, s("args"));
        expr(pp, func, indent);
        printf("(");
        di_size_t n = di_array_length(args);
        for (di_size_t i = 0; i < n; i++) {
            if (i > 0)
                printf(",\n%*s", indent + 4, "");
            expr(pp, di_array_get(args, i), indent + 8);
        }
        printf(")");
    } else if (di_equal(op, s("case"))) {
        di_t subj = di_dict_get(e, s("subj"));
        di_t alts = di_dict_get(e, s("alts"));
        printf("case ");
        expr(pp, subj, indent + 5);
        printf(" of");
        int n = di_array_length(alts);
        for (int i = 0; i < n; i++) {
            di_t alt = di_array_get(alts, i);
            di_t pat = di_dict_get(alt, s("pat"));
            di_t exp = di_dict_get(alt, s("expr"));
            printf("\n%*s", indent + 4, "");
            expr(pp, pat, indent + 4);
            printf(" ->\n%*s", indent + 8, "");
            //printf(" -> ");
            expr(pp, exp, indent + 8);
        }
        printf("\n%*s", indent, "");
    } else if (is_binop(pp, op)) {
        printf("(");
        expr(pp, di_dict_get(e, s("left")), indent + 1);
        printf(" %.*s ", di_string_length(op), di_string_chars(op));
        expr(pp, di_dict_get(e, s("right")), indent + 1);
        printf(")");
    } else if (di_equal(op, s("if"))) {
        printf("if ");
        expr(pp, di_dict_get(e, s("cond")), indent + 3);
        printf("\n%*sthen ", indent + 4, "");
        expr(pp, di_dict_get(e, s("then")), indent + 9);
        printf("\n%*selse ", indent + 4, "");
        expr(pp, di_dict_get(e, s("else")), indent + 9);
    } else if (di_equal(op, s("do"))) {
        printf("do\n");
        exprs(pp, di_dict_get(e, s("seq")), indent + 4);
        printf("\n");
    } else if (di_is_string(op)) {
        printf("<unimplemented expression: ");
        ps(op);
        printf(">");
    } else if (di_is_null(op)) {
        printf("<not an expression>");
    } else {
        printf("<unexpected type of expression type>");
    }
}

void di_prettyprint(di_t tree) {
    assert(di_is_array(tree));
    di_t pp = create_pp();
    exprs(pp, tree, 0);
    printf("\n");
    di_cleanup(pp);
}
