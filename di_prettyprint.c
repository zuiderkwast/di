#include <stdio.h>
#include <stdbool.h>
#include "di.h"
#include "json.h"
#include "di_prettyprint.h"

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
	                "!=", "@", "~", "+", "-", "*", "/", "mod"};
	int i;
	for (i = 0; i < sizeof(ops) / sizeof(char *); i++) {
		di_array_push(&binops, s(ops[i]));
	}
	pp = di_dict_set(pp, s("binops"), binops);
	return pp;
}

static void expr(di_t pp, di_t e, int indent);
static void pattern(di_t pp, di_t pat, int indent);

static void pattern(di_t pp, di_t p, int indent) {
	di_t op = di_dict_get(p, s("pat"));
	if (di_equal(op, s("lit"))) {
		di_t v = di_dict_get(p, s("value"));
		di_t src = json_encode(v);
		ps(src);
	} else if (di_equal(op, s("var"))) {
		di_t name = di_dict_get(p, s("name"));
		ps(name);
	} else if (di_equal(op, s("regex"))) {
		di_t regex = di_dict_get(p, s("regex"));
		printf("/");
		ps(regex);
		printf("/");
	} else {
		printf("<unimplemented pattern>");
	}
	//printf("<pattern unimplemented>");
	//expr(pp, pat, indent);
}

static void expr(di_t pp, di_t e, int indent) {
	di_t op = di_dict_get(e, s("expr"));
	if (di_equal(op, s("lit"))) {
		di_t value = di_dict_get(e, s("value"));
		di_t src = json_encode(value);
		ps(src);
	} else if (di_equal(op, s("var"))) {
		di_t name = di_dict_get(e, s("name"));
		ps(name);
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
			printf("\n%*s", indent + 8, "");
			pattern(pp, pat, indent + 8);
			//printf(" ->\n%*s", indent + 16, "");
			printf(" -> ");
			expr(pp, exp, indent + 16);
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
		printf("\n%*sthen ", indent + 8, "");
		expr(pp, di_dict_get(e, s("then")), indent + 13);
		printf("\n%*selse ", indent + 8, "");
		expr(pp, di_dict_get(e, s("else")), indent + 13);
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
	di_t pp = create_pp();
	expr(pp, tree, 0);
	printf("\n");
}
