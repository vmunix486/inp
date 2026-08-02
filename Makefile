CC = cc
CFLAGS = -Ofast -flto
EMCC = $(HOME)/emsdk/upstream/emscripten/emcc
EMCCFLAGS = -O3 -ffast-math -flto

all: img2inp inpview-sdl2 inpview-x11

inp.o: src/inp.c src/inp.h
	$(CC) $(CFLAGS) -c src/inp.c -o inp.o

# SDL2 frontend
inpview-sdl2.o: src/inpview-sdl2.c src/inp.h
	$(CC) $(CFLAGS) $(shell sdl2-config --cflags) -c src/inpview-sdl2.c -o inpview-sdl2.o

inpview-sdl2: inp.o inpview-sdl2.o
	$(CC) $(CFLAGS) inp.o inpview-sdl2.o -o inpview-sdl2 $(shell sdl2-config --libs)

# X11/Xlib frontend
inpview-x11.o: src/inpview-x11.c src/inp.h
	$(CC) $(CFLAGS) $(shell pkg-config --cflags x11) -c src/inpview-x11.c -o inpview-x11.o

inpview-x11: inp.o inpview-x11.o
	$(CC) $(CFLAGS) inp.o inpview-x11.o -o inpview-x11 $(shell pkg-config --libs x11)

# Converter
img2inp:
	$(CC) $(CFLAGS) -Isrc src/img2inp.c -o img2inp -lm

# Emscripten/WebAssembly
inpview-wasm: src/inp.c src/inpview-sdl2.c src/inp.h shell.html
	$(EMCC) $(EMCCFLAGS) -s USE_SDL=2 -s ALLOW_MEMORY_GROWTH=1 --shell-file shell.html -Isrc src/inp.c src/inpview-sdl2.c -o inpview-wasm.html -lm

update:
	rm -fv docs/inp.html src/stb_image.h
	cd docs && wget http://slow.c2dthinkcentre.com/articles/inp.html
	cd src && wget https://github.com/nothings/stb/raw/refs/heads/master/stb_image.h

clean:
	rm -f img2inp inpview-sdl2 inpview-x11 inp.o inpview-sdl2.o inpview-x11.o
	rm -f inpview-wasm.html inpview-wasm.js inpview-wasm.wasm
