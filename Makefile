# apt install libyajl-dev

CFLAGS += -Wall -std=c99 -pedantic
LDFLAGS += -lyajl -lpcre

.PHONY: all test clean

PROGRAMS = dlc json-dump json-test di-test

all: $(PROGRAMS)

test: json-test
	./json-test

clean:
	rm -rf *.o Makefile.deps

# Don't create dependencies when the target is 'clean'
NODEPS := clean
ifeq (0, $(words $(findstring $(MAKECMDGOALS), $(NODEPS))))
$(shell [ -f Makefile.deps ] || perl makedeps.pl $(patsubst %,%.c,$(PROGRAMS)) > Makefile.deps)
include Makefile.deps
endif
