.PHONY: all clean

CXXFLAGS=-Wall -Werror -g $(shell pkg-config --cflags libpulse jack)
LDLIBS=-lpthread $(shell pkg-config --libs libpulse jack) $(shell pkg-config --variable=server_libs jack)

all: jopa

clean:
	rm -f jopa
