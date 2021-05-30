#CC := gcc
CC := mpicc

DBGFLAGS := -g -Wall -Werror -Wextra -Wshadow -Wunreachable-code \
-Wuninitialized -Wduplicated-cond -Wduplicated-branches -Wpedantic -m64 \
 -I /usr/include -O3
RLSFLAGS := -O3 -DNDEBUG -I /usr/include
CFLAGS   := -fopenmp -lmpi

DEPFLAGS    = -MT $@ -MMD -MP -MF $*.Td
POSTCOMPILE = mv -f $*.Td $*.d && touch $@

ISDBGFLAG := $(filter test, $(MAKECMDGOALS))
REALFLAGS  = $(if $(ISDBGFLAG), $(DBGFLAGS), $(RLSFLAGS))

OMPDIR    := OMP
MPIDIR    := MPI
OMMPPIDIR := parallel_OMP_MPI

OMPOBJ 	   = $(OMPDIR)/exact_cover.o
MPIOBJ 	   = $(MPIDIR)/exact_cover.o
OMMPPIOBJ  = $(OMPMPIDIR)/exact_cover.o
OBJS       = $(OMPOBJ) $(MPIOBJ) $(OMMPPIOBJ)

OMPMESOBJ 	   = $(OMPDIR)/exact_cover_measure.o
MPIMESOBJ 	   = $(MPIDIR)/exact_cover_measure.o
OMMPPIMESOBJ   = $(OMPMPIDIR)/exact_cover_measure.o
OBJS       	   = $(OMPMESOBJ) $(MPIMESOBJ) $(OMMPPIMESOBJ)


#COMPILE 	= $(CC) $(REALFLAGS) $(DEPFLAGS) $(TARGET_ARCH) $(CFLAGS)
COMPILE 	= $(CC) $(REALFLAGS) $(DEPFLAGS) $(TARGET_ARCH) $(CFLAGS)

OMPTRGT    := $(OMPDIR)/exact_cover.exe 
MPITRGT    := $(MPIDIR)/exact_cover.exe
OMMPPITRGT := $(OMMPPIDIR)/exact_cover.exe

OMPMESTRGT    := $(OMPDIR)/exact_cover_measure.exe 
MPIMESTRGT    := $(MPIDIR)/exact_cover_measure.exe
OMMPPIMESTRGT := $(OMMPPIDIR)/exact_cover_measure.exe 


all: $(OMPTRGT) $(MPITRGT) $(OMMPPITRGT)

measures: $(OMPMESTRGT) $(MPIMESTRGT) $(OMMPPIMESTRGT)

%.exe: %.c %.d
	$(COMPILE) -o $@ $<
	$(POSTCOMPILE)

OMP/exact_cover_measure.exe: OMP/exact_cover_measure.c
	$(COMPILE) -o $@ $<
	$(POSTCOMPILE)

MPI/exact_cover_measure.exe: MPI/exact_cover_measure.c
	$(COMPILE) -o $@ $<
	$(POSTCOMPILE)
	
$(OMMPPIDIR)/exact_cover_measure.exe: $(OMMPPIDIR)/exact_cover_measure.c
	$(COMPILE) -o $@ $<
	$(POSTCOMPILE)
	

DEPFILES := $(OMPOBJ:%.o=%.d) $(MPIOBJ:%.o=%.d) $(OMMPPIOBJ:%.o=%.d) $(OMPMESOBJ:%o=%.d) $(MPIMESOBJ:%o=%.d) $(OMMPPIMESOBJ:%o=%.d)
$(DEPFILES):
include $(wildcard $(DEPFILES))

.PHONY: clean test all

clean:
	rm -f $(OBJS) $(OMPTRGT) $(MPITRGT) $(OMMPPITRGT) $(OMPMESTRGT) $(MPIMESTRGT) $(OMMPPIMESTRGT)