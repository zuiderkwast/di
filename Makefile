CFLAGS += -Wall -std=c99 -pedantic -g $(EXTRA_CFLAGS)
LDFLAGS += -g -lpcre $(EXTRA_LDFLAGS)

ifdef ASAN
CFLAGS += -fsanitize=address
LDFLAGS += -fsanitize=address
endif

.PHONY: all test clean

PROGRAMS = dlc di-test
#json-dump json-test

all: $(PROGRAMS)

# Linking dependencies
dlc: dlc.o di_debug.o di_io.o di.o di_prettyprint.o di_annotate.o di_parser.o di_lexer.o
	$(CC) -o dlc $^ $(LDFLAGS)

di-test: di-test.o di.o di_debug.o di_prettyprint.o
	$(CC) -o di-test $^ $(LDFLAGS)

# test: json-test
# 	./json-test

clean:
	rm -rf *.o Makefile.deps

# Don't create dependencies when the target is 'clean'
NODEPS := clean

# Delendencies, including linking dependencies. The script scans for lines on
# the form #include "file.h". If a.c includes b.h, then a.o is linked with b.h.
# The main programs are input to the Perl script.
# ifeq (0, $(words $(findstring $(MAKECMDGOALS), $(NODEPS))))
# $(shell [ -f Makefile.deps ] || perl makedeps.pl $(patsubst %,%.c,$(PROGRAMS)) > Makefile.deps)
# include Makefile.deps
# endif

# Another way to generate dependencies for .o files
# -------------------------------------------------
Makefile.dep:
	$(CC) -MM *.c > $@

ifeq (0, $(words $(findstring $(MAKECMDGOALS), $(NODEPS))))
-include Makefile.dep
endif

# Dependencies for each .o file, when available, generated as a byproduct when
# compiling with the -MMD flag.
DEP = $(PROG_OBJ:%.o=%.d)
-include $(DEP)

%.o: %.c
	$(CC) $(CFLAGS) -MMD -o $@ -c $<
