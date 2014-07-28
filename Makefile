CFLAGS+=-g -ggdb -std=c99 -pedantic -Wall
BIN=bottler

all: ${BIN}

install: all
	mkdir -p ~/bin
	install -m 755 ${BIN} /usr/bin/

config:
	install -m 644 bottler.conf ~/.bottler.conf

clean:
	rm -f ${BIN}
