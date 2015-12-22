#include "di.h"
#include "json.h"
#include <stdio.h>

static di_t IF, THEN, ELSE, LET, IN;
static void init_keywords(void) {
	IF   = di_string_from_cstring("if");
	THEN = di_string_from_cstring("then");
	ELSE = di_string_from_cstring("else");
	LET  = di_string_from_cstring("let");
	IN   = di_string_from_cstring("in");
}

struct formdef {
	di_t keys;
	di_t (*reduce)(di_t scope, di_t form);
} formdef_t;

di_t if_then_else(di_t scope, di_t form) {
	di_t cond = di_dict_get(form, IF);
}

formdef_t builtin_forms[] = {
	{{"if", "then", "else"}, if_then_else}
};

di_t eval(di_t form) {
	if (di_is_dict(form)) {
		
	}
}

int main(int argc, char **argv) {
	init_keywords();
	return 0;
}
