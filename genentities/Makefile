CFLAGS+=-std=c99 -pedantic -Wall -Wextra
LDLIBS=-lcurl -ljansson

BIN=genentities
OBJ=util.o

$(BIN): $(OBJ)

all: $(BIN)

clean:
	rm -f $(BIN) $(OBJ)
