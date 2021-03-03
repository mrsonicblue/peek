# makefile to fail if any command in pipe is failed.
SHELL = /bin/bash -o pipefail

# using gcc version 5.4.1 20161213 (Linaro GCC 5.4-2017.01-rc2)
BASE    = arm-linux-gnueabihf

CC      = $(BASE)-gcc
LD      = $(BASE)-ld
STRIP   = $(BASE)-strip

ifeq ($(V),1)
	Q :=
else
	Q := @
endif

INCLUDE	= -I./ -I/build/lmdb/libraries/liblmdb
LIBS = -L/build/lmdb/libraries/liblmdb

PRJ = peek
C_SRC =   $(wildcard *.c)

CPP_SRC = $(wildcard *.cpp)

OBJ	= $(C_SRC:.c=.c.o) $(CPP_SRC:.cpp=.cpp.o)
DEP	= $(C_SRC:.c=.c.d) $(CPP_SRC:.cpp=.cpp.d)

DFLAGS	= $(INCLUDE) -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE
CFLAGS	= $(DFLAGS) -Wall -Wextra -Wno-strict-aliasing -Wno-unused-parameter -c -O3
LFLAGS	= $(LIBS) -lc -lstdc++ -lrt -lm -lpthread -ldl -llmdb

$(PRJ): $(OBJ)
	$(Q)$(info $@)
	$(Q)$(CC) -o $@ $+ $(LFLAGS)
	$(Q)cp $@ $@.elf
	$(Q)$(STRIP) $@

clean:
	$(Q)rm -f *.elf *.map *.lst *.user *~ $(PRJ)
	$(Q)rm -rf obj DTAR* x64
	$(Q)find . \( -name '*.o' -o -name '*.d' -o -name '*.bak' -o -name '*.rej' -o -name '*.org' \) -exec rm -f {} \;

%.c.o: %.c
	$(Q)$(info $<)
	$(Q)$(CC) $(CFLAGS) -std=gnu99 -o $@ -c $<

%.cpp.o: %.cpp
	$(Q)$(info $<)
	$(Q)$(CC) $(CFLAGS) -std=gnu++14 -o $@ -c $<

-include $(DEP)
%.c.d: %.c
	$(Q)$(CC) $(DFLAGS) -MM $< -MT $@ -MT $*.c.o -MF $@

%.cpp.d: %.cpp
	$(Q)$(CC) $(DFLAGS) -MM $< -MT $@ -MT $*.cpp.o -MF $@
