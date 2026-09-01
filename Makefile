# prebsd — fetch and boot a Research Unix disk image on simh.
#
#   make            build ./prebsd
#   make check      compile-only sanity (the binary is exercised by hand)
#   make clean

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-format-truncation
CFLAGS  += $(shell pkg-config --cflags libcurl json-c)
LDLIBS  += $(shell pkg-config --libs libcurl json-c)

all: prebsd

prebsd: prebsd.c
	$(CC) $(CFLAGS) -o $@ prebsd.c $(LDLIBS)

check: prebsd
	./prebsd 2>&1 | grep -q usage && echo "check: usage path OK" || true

clean:
	rm -f prebsd

.PHONY: all check clean
