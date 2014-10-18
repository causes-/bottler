CFLAGS+=-std=c99 -pedantic -Wall -Wextra
LDLIBS=-lcurl

BIN=bottler

all: $(BIN)

clean:
	rm -f $(BIN) $(OBJ)

install: all
	mkdir -p $(DESTDIR)/usr/bin
	install -m 755 $(BIN) $(DESTDIR)/usr/bin/

uninstall:
	rm -f $(DESTDIR)/usr/bin/$(BIN)
