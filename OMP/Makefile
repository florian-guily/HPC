
CC=gcc
CFLAGS=-I.
OBJ = exact_cover.o
OBJ_OR = exact_cover_original.o
INSTANCES = ../Instances

exact_cover: exact_cover.c
	$(CC) $< -o $@.out -O3 -fopenmp


all: exact_cover exact_cover_original

exact_cover_original: exact_cover_original.c
	$(CC) $< -o $@.out -O3

exec: exact_cover
	./exact_cover.out --in $(INSTANCES)/bell12.ec
	./exact_cover_original.out --in $(INSTANCES)/bell12.ec 

.PHONY: clean

clean:
	rm -f exact_cover.out exact_cover_original.out