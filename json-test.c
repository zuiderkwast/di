#include <stdlib.h>
#include <stdio.h>
#include "di.h"
#include "json.h"

/*
 * gcc -Wall -I/opt/local/include json-test.c json.c di.c -lyajl -o json-test
 */

int main(int argc, char ** argv) {
	char *input;
	di_t str, json;
	if (argc >= 2)
		input = argv[1];
	else
		input = "brännvin";

	str = di_string_from_cstring(input);
	// json is free'd by json_decode since the refc == 0.

	json = json_encode(str);
	if (di_is_undefined(json)) {
		printf("Failed (1) to convert parsed data to JSON\n");
		exit(1);
	}
	printf("1. %.*s\n", di_string_length(json), di_string_chars(json));

	json = json_encode(str);
	if (di_is_undefined(json)) {
		printf("Failed (2) to convert parsed data to JSON\n");
		exit(2);
	}
	printf("2. %.*s\n", di_string_length(json), di_string_chars(json));

	di_cleanup(str);
	di_cleanup(json);
	return 0;
}
