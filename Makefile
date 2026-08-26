# runv7 — fetch + boot V6/V7 disk images headlessly on simh.
PREFIX ?= /usr/local
BINDIR  = $(PREFIX)/bin
LIBDIR  = $(PREFIX)/lib/runv7
MANDIR  = $(PREFIX)/share/man/man1

all:
	@echo "runv7 is a set of scripts; nothing to compile."
	@echo "Usage: ./fetch <name>;  ./boot.py --ini ini/<name>.ini"

check:
	./fetch

install:
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(LIBDIR)/ini $(DESTDIR)$(MANDIR)
	install -m755 fetch boot.py $(DESTDIR)$(LIBDIR)/
	install -m755 fetch $(DESTDIR)$(BINDIR)/runv7-fetch
	install -m644 images.tsv $(DESTDIR)$(LIBDIR)/
	install -m644 ini/*.ini $(DESTDIR)$(LIBDIR)/ini/
	install -m644 man/*.1 $(DESTDIR)$(MANDIR)/

uninstall:
	rm -rf $(DESTDIR)$(LIBDIR)
	rm -f $(DESTDIR)$(BINDIR)/runv7-fetch $(DESTDIR)$(MANDIR)/fetch.1 $(DESTDIR)$(MANDIR)/boot.1

clean:
	rm -f *.o *.log

dist: clean
	mkdir -p runv7-0.1
	cp -r fetch boot.py images.tsv ini man README.md COPYING AUTHORS NEWS ChangeLog INSTALL Makefile runv7-0.1/
	tar czf runv7-0.1.tar.gz runv7-0.1
	rm -rf runv7-0.1

.PHONY: all check install uninstall clean dist
