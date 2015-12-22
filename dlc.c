#include <stdio.h>
#include "di.h"
#include "di_lexer.h"
#include "di_parser.h"
#include "di_prettyprint.h"
#include "di_io.h"
#include "json.h"
#include "di_debug.h"

/*
 * gcc -Wall -I/opt/local/include dlc.c di.c di_lexer.c di_io.c json.c -L/opt/local/lib -lpcre -lyajl -o dlc
 */

/*

Lexer -> Layout processor -> Parser -> Type and variable access annotator -> Compiler

*/


/**
 * Like any di function, frees the token if its refc == 0.
 */
static inline void debug_dump(const char *label, di_t token) {
	/*
	// json encode
	if (!di_is_pointer(token))
		printf("Debug_dump of non-pointer\n");
	//printf("Debug dump -- length %d\n", di_(token));
	assert(di_is_dict(token));
	di_t json = json_encode(token);
	if (di_is_undefined(json)) {
		printf("Failed to convert data to JSON\n");
		exit(2);
	}
	printf("%s%.*s\n", label,
	       di_string_length(json), di_string_chars(json));
	di_cleanup(json);
	*/
	printf("%s full dump:\n", label);
	di_dump(token, 0);
	printf("\n");
}

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "Usage: %s [COMMAND] FILENAME\n", argv[0]);
		fprintf(stderr, "Commands: source, lex, parse\n");
		exit(1);
	}
	char *cmd, *fn;
	if (argc == 2) {
		cmd = "lex";
		fn = argv[1];
	} else {
		cmd = argv[1];
		fn = argv[2];
	}
	di_t filename = di_string_from_cstring(fn);
	di_t source = di_readfile(filename);
	if (!strcmp("source", cmd)) {
		debug_dump("Source: ", source);
	} else if (!strcmp("lex", cmd)) {
		di_t lexer = di_lexer_create(source);
		debug_dump("Lexer: ", lexer);
		di_t token = di_null();
		di_t op;
		bool accept_regex = true;
		do {
			token = di_lex(&lexer, token, accept_regex);
			op = di_dict_get(token, di_string_from_cstring("op"));
			debug_dump("Token: ", token);
		} while (!di_equal(op, di_string_from_cstring("eof")));
	} else if (!strcmp("parse", cmd)) {
		di_t tree = di_parse(source);
		printf("Parsing done.\n");
		debug_dump("Parse tree: ", tree);
	} else if (!strcmp("pp", cmd)) {
		di_t tree = di_parse(source);
		di_prettyprint(tree);
	} else {
		fprintf(stderr, "Bad command: %s\n", cmd);
		exit(1);
	}

	return 0;
}
