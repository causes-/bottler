CFLAGS += -std=c99 -pedantic -Wall -Wextra
LDLIBS = -lcurl
PREFIX = /usr/local

BIN = bottler
OBJ = util.o gettitle.o bottler.o
CFG = config.h

$(BIN): $(OBJ)

$(OBJ): $(CFG)

all: $(BIN)

config.h:
	cp config.def.h config.h

clean:
	rm -f $(BIN) $(OBJ)

distclean:
	rm -f $(BIN) $(OBJ) $(CFG)

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f $(BIN) $(DESTDIR)$(PREFIX)/bin/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)
