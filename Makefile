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

OMPOBJ 	  := $(filter-out $(OMPDIR)/exact_cover_original.o,    $(patsubst %.c, %.o, $(wildcard $(OMPDIR)/*.c)))
MPIOBJ 	  := $(filter-out $(MPIDIR)/exact_cover_original.o,    $(patsubst %.c, %.o, $(wildcard $(MPIDIR)/*.c)))
OMMPPIOBJ := $(filter-out $(OMMPPIDIR)/exact_cover_original.o, $(patsubst %.c, %.o, $(wildcard $(OMMPPIDIR)/*.c)))
OBJS       = $(OMPOBJ) $(MPIOBJ) $(OMMPPIOBJ)


#COMPILE 	= $(CC) $(REALFLAGS) $(DEPFLAGS) $(TARGET_ARCH) $(CFLAGS)
COMPILE 	= $(CC) $(REALFLAGS) $(DEPFLAGS) $(TARGET_ARCH) $(CFLAGS)

OMPTRGT    := $(OMPDIR)/exact_cover.exe 
MPITRGT    := $(MPIDIR)/exact_cover.exe #$(MPIDIR)/exact_cover_server.exe
OMMPPITRGT := $(OMMPPIDIR)/exact_cover.exe #$(OMMPPIDIR)/exact_cover_server.exe 
#Is it necessary? Someone has to take the lead, right?


all: $(OMPTRGT) $(MPITRGT) $(OMMPPITRGT)

%.exe: %.c %.d
	$(COMPILE) -o $@ $<
	$(POSTCOMPILE)


DEPFILES := $(OMPOBJ:%.o=%.d) $(MPIOBJ:%.o=%.d) $(OMMPPIOBJ:%.o=%.d)
$(DEPFILES):
include $(wildcard $(DEPFILES))

.PHONY: clean test all

clean:
	rm -f $(OBJS) $(OMPTRGT) $(MPITRGT) $(OMMPPITRGT)