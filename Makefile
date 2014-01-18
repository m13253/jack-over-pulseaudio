
.PHONY: all clean

CC=gcc
LIB=-lpthread $(shell pkg-config --cflags --libs libpulse-simple || echo -lpulse-simple) $(shell pkg-config --cflags --libs jack || echo -ljack)
CFLAGS=-Wall -Werror -O3 $(LIB) $(OPT)

all: jopa

clean:
	rm -f jopa

jopa: jopa.c
	$(CC) -o "$@" "$<" $(CFLAGS)

