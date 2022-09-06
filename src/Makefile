CC = gcc

CFLAGS = -O3 -mtune=native -march=native
override CFLAGS += -Ilib
override LDFLAGS += -lm

OUT = ryzen_monitor

SRC = ryzen_monitor.c
SRC += pm_tables.c
SRC += readinfo.c
SRC += setinfo.c
SRC += commonfuncs.c
SRC += argparse.c
SRC += lib/libsmu.c

OBJ = $(SRC:.c=.o)

all: $(OUT)

debug: CFLAGS += -DDEBUG -g
debug: all 


$(OUT): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(OUT) $(OBJ)

ifeq ($(PREFIX),)
    PREFIX := /usr/local
endif

.PHONY: install
install:
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 ryzen_monitor $(DESTDIR)$(PREFIX)/bin

clean:
	rm -rf *.o lib/*.o
