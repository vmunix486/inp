CC = cc
CFLAGS = -Ofast -flto

all: img2inp inpview

img2inp:
	$(CC) $(CFLAGS) -Isrc src/img2inp.c -o img2inp -lm

inpview:
	$(CC) $(CFLAGS) src/inpview.c -o inpview $(shell sdl2-config --cflags --libs)

update:
	rm -fv docs/inp.html src/stb_image.h
	cd docs && wget http://slow.c2dthinkcentre.com/articles/inp.html
	cd src && wget https://github.com/nothings/stb/raw/refs/heads/master/stb_image.h

clean:
	rm -f img2inp inpview
