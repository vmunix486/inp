CC = cc
CFLAGS = -Ofast -flto

all: img2inp inpview

inp.o: src/inp.c src/inp.h
	$(CC) $(CFLAGS) -c src/inp.c -o inp.o

inpview.o: src/inpview.c src/inp.h
	$(CC) $(CFLAGS) $(shell sdl2-config --cflags) -c src/inpview.c -o inpview.o

inpview: inp.o inpview.o
	$(CC) $(CFLAGS) inp.o inpview.o -o inpview $(shell sdl2-config --libs)

img2inp:
	$(CC) $(CFLAGS) -Isrc src/img2inp.c -o img2inp -lm

update:
	rm -fv docs/inp.html src/stb_image.h
	cd docs && wget http://slow.c2dthinkcentre.com/articles/inp.html
	cd src && wget https://github.com/nothings/stb/raw/refs/heads/master/stb_image.h

clean:
	rm -f img2inp inpview inp.o inpview.o
