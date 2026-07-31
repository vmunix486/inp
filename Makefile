CC = cc
CFLAGS = -Ofast -flto

all: img2inp inpview

img2inp:
	$(CC) $(CFLAGS) img2inp.c -o img2inp -lm

inpview:
	$(CC) $(CFLAGS) inpview.c -o inpview $(shell sdl2-config --cflags --libs)

clean:
	rm -f img2inp inpview
