
.PHONY: all clean

LIB=-lpulse-simple $(shell pkg-config --cflags --libs jack || echo -ljack)
CFLAGS=-Wall -Werror -O3 $(LIB)

all: jopa

clean:
	rm -f jopa

jopa: jopa.c
	gcc -o "$@" "$<" $(CFLAGS)

