#include <stdio.h>
#include "di.h"
#include "di_lexer.h"
#include "di_parser.h"
#include "di_prettyprint.h"
#include "di_io.h"
#include "di_debug.h"

/*

Lexer -> Layout processor -> Parser -> Type and variable access annotator -> Compiler

*/


/**
 * Like any di function, frees the token if its refc == 0.
 */
static inline void debug_dump(const char *label, di_t token) {
	printf("%s", label);
	di_dump(token, 0);
	printf("\n");
}

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "Usage: %s [COMMAND] FILENAME\n", argv[0]);
		fprintf(stderr, "Commands: source, lex, parse, pp\n");
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
        di_cleanup(filename);
	if (!strcmp("source", cmd)) {
		debug_dump("Source: ", source);
	} else if (!strcmp("lex", cmd)) {
		di_t lexer = di_lexer_create(source);
                //di_cleanup(source); // should be done by di_lexer_create(source);
		debug_dump("Lexer: ", lexer);
		di_t token = di_null();
		di_t op;
		do {
			token = di_lex(&lexer, token);
			op = di_dict_get(token, di_string_from_cstring("op"));
			debug_dump("Token: ", token);
		} while (!di_equal(op, di_string_from_cstring("eof")));
                di_cleanup(token);
                di_cleanup(lexer);
	} else if (!strcmp("parse", cmd)) {
		di_t tree = di_parse(source);
		printf("Parsing done.\n");
		debug_dump("Parse tree: ", tree);
                di_cleanup(tree);
	} else if (!strcmp("pp", cmd)) {
		di_t tree = di_parse(source);
		di_prettyprint(tree);
                di_cleanup(tree);
	} else {
		fprintf(stderr, "Bad command: %s\n", cmd);
		exit(1);
	}

	return 0;
}
