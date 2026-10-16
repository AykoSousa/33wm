PREFIX ?= /usr/local
DESTDIR ?=
BINDIR = $(PREFIX)/bin
DATADIR = $(PREFIX)/share/33wm
SESSIONDIR = $(PREFIX)/share/xsessions

CC ?= cc
CFLAGS ?= -O2
CFLAGS += -Wall -Wextra -DBAR_SCRIPT=\"$(DATADIR)/bar.sh\"
LDLIBS = -lxcb -lxcb-keysyms -lxcb-randr

SRC = main.c wm.c layout.c config.c
OBJ = $(SRC:.c=.o)
HDR = wm.h layout.h config.h

all: 33wm 33wm.desktop

33wm: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c $(HDR)
	$(CC) $(CFLAGS) -c $< -o $@

# Entrada de sessão para gerenciadores de login (SDDM, LightDM...), com o caminho do binário
33wm.desktop: 33wm.desktop.in
	sed 's|@BINDIR@|$(BINDIR)|g' 33wm.desktop.in > $@

install: 33wm 33wm.desktop
	install -Dm755 33wm $(DESTDIR)$(BINDIR)/33wm
	install -Dm755 bar.sh $(DESTDIR)$(DATADIR)/bar.sh
	install -Dm644 33wm.desktop $(DESTDIR)$(SESSIONDIR)/33wm.desktop

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/33wm
	rm -f $(DESTDIR)$(DATADIR)/bar.sh
	rm -f $(DESTDIR)$(SESSIONDIR)/33wm.desktop

clean:
	rm -f 33wm $(OBJ) 33wm.desktop

.PHONY: all install uninstall clean
