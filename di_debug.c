#include "di.h"
#include "json.h"
#include <stdio.h>
#include "di_debug.h"

static inline void print_indent(int indent) {
	int i;
	for (i = 0; i < indent; i++)
		printf("  ");
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
				print_indent(indent + 1);
				di_dump(di_array_get(v, i), indent + 1);
				printf("\n");
			}
			print_indent(indent);
			printf("]");
			return;
		} else if (di_is_dict(v)) {
			printf("{\n");
			di_t key, value;
			for (i = 0; (i = di_dict_iter(v, i, &key, &value)) != 0;) {
				print_indent(indent + 1);
				di_dump(key, indent + 1);
				printf(": ");
				di_dump(value, indent + 1);
				printf("\n");
			}
			print_indent(indent);
			printf("}");
			return;
		}
	}
	di_t json = json_encode(v);
	printf("%.*s", di_string_length(json), di_string_chars(json));
	di_cleanup(json);
}

void di_debug(char * const prefix, di_t value) {
	printf("%s", prefix);
	di_dump(value, 0);
	printf("\n");
}
