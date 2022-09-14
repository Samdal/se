# st - simple terminal
# See LICENSE file for copyright and license details.
.POSIX:

include config.mk

SRC = se.c x.c config.c buffer.c seek.c utf8.c
OBJ = $(SRC:.c=.o)

all: options se

options:
	@echo se build options:
	@echo "CFLAGS  = $(SECFLAGS)"
	@echo "LDFLAGS = $(SELDFLAGS)"
	@echo "CC      = $(CC)"

config.c:
	cp config.def.c config.c

.c.o:
	$(CC) $(SECFLAGS) -c $<

se.o: se.h x.h
x.o: se.h x.h

$(OBJ): config.c config.mk

se: $(OBJ)
	$(CC) -o $@ $(OBJ) $(SELDFLAGS)

clean:
	rm -f se $(OBJ) se-$(VERSION).tar.gz

dist: clean
	mkdir -p se-$(VERSION)
	cp -R LICENSE Makefile README config.mk\
		config.def.c se.h x.h $(SRC)\
		se-$(VERSION)
	tar -cf - se-$(VERSION) | gzip > se-$(VERSION).tar.gz
	rm -rf se-$(VERSION)

install: se
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f se $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/se
#	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
#	sed "s/VERSION/$(VERSION)/g" < se.1 > $(DESTDIR)$(MANPREFIX)/man1/st.1
# chmod 644 $(DESTDIR)$(MANPREFIX)/man1/st.1
#	tic -sx se.info
#	@echo Please see the README file regarding the terminfo entry of se.

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/se
#	rm -f $(DESTDIR)$(MANPREFIX)/man1/st.1

run: all
	./se

.PHONY: all options clean dist install uninstall run
