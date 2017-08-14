.PHONY: all clean install

CXXFLAGS=-std=gnu++11 -O3 -Wall -g $(shell pkg-config --cflags libpulse jack)
LDLIBS=-lpthread $(shell pkg-config --libs libpulse jack)
PREFIX=/usr/local

all: jopa

clean:
	rm -f jopa

install: all
	install -Dm0755 jopa $(DESTDIR)$(PREFIX)/bin/jopa
