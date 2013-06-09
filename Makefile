
.PHONY: all clean

LIB=-lpthread $(shell pkg-config --cflags --libs libpulse-simple || echo -lpulse-simple) $(shell pkg-config --cflags --libs jack || echo -ljack)
CFLAGS=-Wall -Werror -O3 $(LIB)

all: jopa

clean:
	rm -f jopa

jopa: jopa.c
	gcc -o "$@" "$<" $(CFLAGS)

