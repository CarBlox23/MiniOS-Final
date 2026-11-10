CC      = gcc
CFLAGS  = -Wall -Wextra -Iinclude
LDFLAGS = -pthread
SRC     = $(wildcard src/*.c)
OBJ     = $(SRC:src/%.c=build/%.o)

minios: $(OBJ)
	$(CC) $(OBJ) -o minios $(LDFLAGS)

build/%.o: src/%.c include/*.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build minios

distclean: clean
	rm -rf datos/minios.disk datos/partida_*.sav datos/matriz.dat

.PHONY: clean distclean
