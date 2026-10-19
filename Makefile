PREFIX ?= /usr/local
DESTDIR ?=
BINDIR = $(PREFIX)/bin
DATADIR = $(PREFIX)/share/33wm
SESSIONDIR = $(PREFIX)/share/xsessions

CC ?= cc
CFLAGS ?= -O2
CFLAGS += -Wall -Wextra -DBAR_SCRIPT=\"$(DATADIR)/33bar\"
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

install: 33wm 33wm.desktop install-bar
	install -Dm755 33wm $(DESTDIR)$(BINDIR)/33wm
	install -Dm644 33wm.desktop $(DESTDIR)$(SESSIONDIR)/33wm.desktop

# Só a barra (bar/33bar), sem recompilar o WM. Não precisa de make: basta copiar o script
install-bar:
	install -Dm755 bar/33bar $(DESTDIR)$(DATADIR)/33bar

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/33wm
	rm -f $(DESTDIR)$(DATADIR)/33bar
	rm -f $(DESTDIR)$(SESSIONDIR)/33wm.desktop

clean:
	rm -f 33wm $(OBJ) 33wm.desktop

.PHONY: all install install-bar uninstall clean
