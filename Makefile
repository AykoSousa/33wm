PREFIX ?= /usr/local
DESTDIR ?=
BINDIR = $(PREFIX)/bin
DATADIR = $(PREFIX)/share/33wm

CC ?= cc
CFLAGS ?= -O2
CFLAGS += -Wall -Wextra -DBAR_SCRIPT=\"$(DATADIR)/bar.sh\"
LDLIBS = -lxcb -lxcb-keysyms -lxcb-randr

SRC = main.c wm.c layout.c config.c
OBJ = $(SRC:.c=.o)
HDR = wm.h layout.h config.h

all: 33wm

33wm: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c $(HDR)
	$(CC) $(CFLAGS) -c $< -o $@

install: 33wm
	install -Dm755 33wm $(DESTDIR)$(BINDIR)/33wm
	install -Dm755 bar.sh $(DESTDIR)$(DATADIR)/bar.sh

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/33wm
	rm -f $(DESTDIR)$(DATADIR)/bar.sh

clean:
	rm -f 33wm $(OBJ)

.PHONY: all install uninstall clean
