SHELL = /bin/bash -o pipefail
BASE = arm-linux-gnueabihf

CC = $(BASE)-gcc
LD = $(BASE)-ld
STRIP = $(BASE)-strip

ifeq ($(V),1)
	Q :=
else
	Q := @
endif

INCLUDE	= -I./ -I../shared -I/build/libfuse/include -I/build/lmdb/libraries/liblmdb
LIBS = /build/libfuse/lib/.libs/libfuse.so -L/build/lmdb/libraries/liblmdb

PRJ = peekfs
C_SRC = $(wildcard *.c) $(wildcard ../shared/*.c)

OBJ	= $(C_SRC:.c=.c.o) $(CPP_SRC:.cpp=.cpp.o)

DFLAGS = $(INCLUDE) -DHAVE_CONFIG_H -D_FILE_OFFSET_BITS=64 -D_REENTRANT
CFLAGS = $(DFLAGS) -Wall -W -Wno-sign-compare -Wstrict-prototypes -Wmissing-declarations -Wwrite-strings -g -O2 -fno-strict-aliasing
LFLAGS = $(LIBS) -pthread -Wl,-rpath -Wl,/build/gcc/lib -llmdb

$(PRJ): $(OBJ)
	$(Q)$(info $@)
	$(Q)$(CC) -o $@ $+ $(LFLAGS)
	$(Q)cp $@ $@.elf
	$(Q)$(STRIP) $@

clean:
	$(Q)rm -f ../shared/*.o ../shared/*.elf
	$(Q)rm -f *.o *.elf $(PRJ)

%.c.o: %.c
	$(Q)$(info $<)
	$(Q)$(CC) $(CFLAGS) -std=gnu99 -o $@ -c $<
