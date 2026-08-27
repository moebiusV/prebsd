# prebsd - fetch + boot V6/V7 disk images headlessly on simh.
PREFIX ?= /usr/local
BINDIR  = $(PREFIX)/bin
LIBDIR  = $(PREFIX)/lib/prebsd
MANDIR  = $(PREFIX)/share/man/man1

all:
	@echo "prebsd is a set of scripts; nothing to compile."
	@echo "Usage: ./fetch <name>;  ./boot.py --ini ini/<name>.ini"

check:
	./fetch

install:
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(LIBDIR)/ini $(DESTDIR)$(MANDIR)
	install -m755 fetch boot.py $(DESTDIR)$(LIBDIR)/
	install -m755 fetch $(DESTDIR)$(BINDIR)/prebsd-fetch
	install -m644 images.tsv $(DESTDIR)$(LIBDIR)/
	install -m644 ini/*.ini $(DESTDIR)$(LIBDIR)/ini/
	install -m644 man/*.1 $(DESTDIR)$(MANDIR)/

uninstall:
	rm -rf $(DESTDIR)$(LIBDIR)
	rm -f $(DESTDIR)$(BINDIR)/prebsd-fetch $(DESTDIR)$(MANDIR)/fetch.1 $(DESTDIR)$(MANDIR)/boot.1

clean:
	rm -f *.o *.log

dist: clean
	mkdir -p prebsd-0.1
	cp -r fetch boot.py images.tsv ini man README.md COPYING AUTHORS NEWS ChangeLog INSTALL Makefile prebsd-0.1/
	tar czf prebsd-0.1.tar.gz prebsd-0.1
	rm -rf prebsd-0.1

.PHONY: all check install uninstall clean dist
