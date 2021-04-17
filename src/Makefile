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

INCLUDE	= -I./ -I../shared -I/build/lmdb/libraries/liblmdb
LIBS = -L/build/lmdb/libraries/liblmdb

PRJ = peek
C_SRC = $(wildcard *.c) $(wildcard ../shared/*.c)

OBJ	= $(C_SRC:.c=.c.o)

DFLAGS = $(INCLUDE) -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE
CFLAGS = $(DFLAGS) -Wall -Wextra -Wno-strict-aliasing -Wno-unused-parameter -c -O3 -fPIC
LFLAGS = $(LIBS) -lc -lstdc++ -lrt -lm -lpthread -ldl -llmdb

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