CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=gnu99
A4 = -I/local/courses/csse2310/include -L/local/courses/csse2310/lib -lcsse2310a4
.PHONY = debug clean
DEBUG = -g
THREAD = -pthread
FREEIMAGE = -I/local/courses/csse2310/include -L/local/courses/csse2310/lib -lcsse2310_freeimage -lfreeimage
	

all: uqimageclient uqimageproc
	
uqimageclient: uqimageclient.c
	$(CC) $(CFLAGS) $(DEBUG) $(A4) -o $@ $<

uqimageproc: uqimageproc.c
	$(CC) $(CFLAGS) $(DEBUG) $(A4) $(THREAD) $(FREEIMAGE) -o $@ $<

clean:
	rm -f uqimageclient
	rm -f uqimageproc
