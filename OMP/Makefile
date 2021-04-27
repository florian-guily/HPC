
CC=gcc
CFLAGS=-I.
OBJ = exact_cover.o
OBJ_OR = exact_cover_original.o

exact_cover: exact_cover.c
	$(CC) $< -o $@.out -O3 -fopenmp


all: exact_cover exact_cover_original

exact_cover_original: exact_cover_original.c
	$(CC) $< -o $@.out -O3 

.PHONY: clean

clean:
	rm -f exact_cover.out exact_cover_original.out