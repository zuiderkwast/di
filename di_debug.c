#include "di.h"
#include "di_prettyprint.h"
#include <stdio.h>
#include "di_debug.h"

#define STEP 2

static inline void print_indent(int indent) {
        printf("%*s", indent, "");
}

void di_dump(di_t v, int indent) {
	// pointer types
	if (di_is_pointer(v)) {
		di_tagged_t *d = di_to_pointer(v);
		di_size_t i;
		printf("|tag=%#x refc=%d %p| ", d->tag, d->refc, (void*)d);
		if (di_is_array(v)) {
			printf("[\n");
			for (i = 0; i < di_array_length(v); i++) {
				print_indent(indent + STEP);
				di_dump(di_array_get(v, i), indent + STEP);
				printf("\n");
			}
			print_indent(indent);
			printf("]");
			return;
		} else if (di_is_dict(v)) {
			printf("{\n");
			di_t key, value;
			for (i = 0; (i = di_dict_iter(v, i, &key, &value)) != 0;) {
				print_indent(indent + STEP);
				di_dump(key, indent + STEP);
				printf(": ");
				di_dump(value, indent + STEP);
				printf("\n");
			}
			print_indent(indent);
			printf("}");
			return;
		}
	}
	di_t lit = di_to_source(v, indent); // json_encode(v);
	printf("%.*s", di_string_length(lit), di_string_chars(lit));
	di_cleanup(lit);
}

void di_debug(char * const prefix, di_t value) {
	printf("%s", prefix);
	di_dump(value, 0);
	printf("\n");
}
