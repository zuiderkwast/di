#include <stdlib.h>
#include <stdio.h>
#include "di.h"
#include "json.h"

/*
 * gcc -Wall json-test.c json.c di.c -lyajl -o json-test
 */

int main(int argc, char ** argv) {
	if (argc != 2) {
		printf("Usage: %s JSONDATA\n", argv[0]);
		exit(1);
	}
	di_t json = di_string_from_chars(argv[1], strlen(argv[1]));
	di_t value = json_decode(json);
	// json is free'd by json_decode since the refc == 0.

	if (di_is_undefined(value)) {
		printf("Invalid JSON\n");
		exit(1);
	}
	di_t json_again = json_encode(value);
	di_cleanup(value);
	if (di_is_undefined(json_again)) {
		printf("Failed to convert parsed data back to JSON\n");
		exit(2);
	}
	printf("%s\n", di_string_chars(json_again));
	di_cleanup(json_again);
	return 0;
}
