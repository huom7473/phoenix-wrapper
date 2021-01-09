CC=gcc
CFLAGS=-Wall -Wextra
LINK=-lm

.PHONY: clean

default: wrapper

wrapper: wrapper.c
	@$(CC) -o wrapper $(CFLAGS) wrapper.c $(LINK)

clean:
	@rm -f wrapper
