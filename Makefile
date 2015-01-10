CFLAGS+=-std=c99 -pedantic -Wall -Wextra
LDLIBS=-lcurl

BIN=bottler
OBJ=util.o gettitle.o bottler.o
CFG=config.h

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
	mkdir -p $(DESTDIR)/usr/bin
	install -m 755 $(BIN) $(DESTDIR)/usr/bin/

uninstall:
	rm -f $(DESTDIR)/usr/bin/$(BIN)
