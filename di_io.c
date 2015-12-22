#include "di_io.h"
#include <assert.h>
#include <stdio.h>

static FILE * di_fopen(const di_t filename, const char *mode) {
	char * fn;
	char buf[7];
	assert(di_is_string(filename));
	di_size_t name_len = di_string_length(filename);
	if (name_len < 7) {
		// It's a shortstring, which is not nul-terminated. Copy and
		// append a nul-terminator.
		char * chars = di_string_chars(filename);
		memcpy((void*)buf, (void*)chars, name_len);
		buf[name_len] = '\0';
		fn = buf;
	}
	else {
		// Nul-terminated. Just point fn to the char contents.
		fn = di_string_chars(filename);
	}
	FILE * f = fopen(fn, mode);
	if (!f) {
		fprintf(stderr, "Can't open file %s in mode %s\n", fn, mode);
		exit(1);
	}
	return f;
}

di_t di_readfile(di_t filename) {
	FILE *f = di_fopen(filename, "r");
	fseek(f, 0L, SEEK_END);
	long size = ftell(f);
	rewind(f);
	if (size < 0) {
		fprintf(stderr, "Can't check filesize\n");
		fclose(f);
		exit(1);
	}
	di_size_t max_size = (di_size_t)-1;
	if (size > max_size) {
		fprintf(stderr, "File too large\n");
		fclose(f);
		exit(1);
	}
	//printf("File size: %ld\n", size);
	di_t contents = di_string_create_presized((di_size_t)size);
	char * dst = di_string_chars(contents);
	size_t nread = fread((void*)dst, (size_t)size, 1, f);
	if (!nread) {
		fprintf(stderr, "Can't read the file contents.\n");
		fclose(f);
		di_cleanup(contents);
		exit(1);
	}
	return contents;
}
