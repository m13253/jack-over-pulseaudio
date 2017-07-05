.PHONY: all clean

CXXFLAGS=-std=gnu++11 -Wall -g $(shell pkg-config --cflags libpulse jack)
LDLIBS=-lpthread $(shell pkg-config --libs libpulse jack)

all: jopa

clean:
	rm -f jopa
