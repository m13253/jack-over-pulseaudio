
.PHONY: all clean

CC=gccj
LIB=-lpthread $(shell pkg-config --cflags --libs libpulse || echo -lpulse) $(shell pkg-config --cflags --libs jack || echo -ljack)
CFLAGS=-Wall -Werror -O3 $(LIB)

all: jopa

clean:
	rm -f jopa

jopa: jopa.c
	$(CC) -o "$@" "$<" $(CFLAGS)

