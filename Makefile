CFLAGS+=-std=c99 -pedantic -Wall -Wextra
LDLIBS=-lcurl

BIN=bottler
OBJ=config.h

all: $(OBJ) $(BIN)

config.h:
	cp config.def.h config.h

clean:
	rm -f $(BIN)

install: all
	mkdir -p $(DESTDIR)/usr/bin
	install -m 755 $(BIN) $(DESTDIR)/usr/bin/

uninstall:
	rm -f $(DESTDIR)/usr/bin/$(BIN)
