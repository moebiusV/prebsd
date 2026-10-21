# prebsd -- fetch and boot old system images on simh.
#
# The program, the scripts, the images.json manifest, simh.json and the ini/ configs live
# together under $(libdir)/prebsd: prebsd finds its files from where the binary really is.
# The user-facing entry points are symlinks in $(bindir).

prebsddir = $(libdir)/prebsd

bin_PROGRAMS =
noinst_PROGRAMS = prebsd
prebsd_SOURCES = prebsd.c
prebsd_CFLAGS = $(CURL_CFLAGS) $(JSON_CFLAGS)
prebsd_LDADD = $(CURL_LIBS) $(JSON_LIBS)

install-data-hook:
	mkdir -p $(DESTDIR)$(prebsddir)/ini $(DESTDIR)$(prebsddir)/simh $(DESTDIR)$(prebsddir)/pdp10 \
	    $(DESTDIR)$(bindir) $(DESTDIR)$(mandir)/man1
	$(INSTALL_PROGRAM) prebsd$(EXEEXT) $(DESTDIR)$(prebsddir)/prebsd
	$(INSTALL_SCRIPT) $(srcdir)/fetch        $(DESTDIR)$(prebsddir)/fetch
	$(INSTALL_SCRIPT) $(srcdir)/build-simh   $(DESTDIR)$(prebsddir)/build-simh
	$(INSTALL_SCRIPT) $(srcdir)/mirror-built $(DESTDIR)$(prebsddir)/mirror-built
	$(INSTALL_SCRIPT) $(srcdir)/mirror-fill  $(DESTDIR)$(prebsddir)/mirror-fill
	$(INSTALL_SCRIPT) $(srcdir)/pdp10/install-ki.sh $(DESTDIR)$(prebsddir)/pdp10/install-ki.sh
	$(INSTALL_DATA)   $(srcdir)/images.json  $(DESTDIR)$(prebsddir)/images.json
	$(INSTALL_DATA)   $(srcdir)/simh.json    $(DESTDIR)$(prebsddir)/simh.json
	$(INSTALL_DATA)   $(srcdir)/simh/patch-sim-tape.py $(DESTDIR)$(prebsddir)/simh/
	for f in $(srcdir)/ini/*.ini; do \
		$(INSTALL_DATA) "$$f" $(DESTDIR)$(prebsddir)/ini/; \
	done
	$(INSTALL_DATA) $(srcdir)/man/prebsd.1    $(DESTDIR)$(mandir)/man1/prebsd.1
	$(INSTALL_DATA) $(srcdir)/man/fetch.1     $(DESTDIR)$(mandir)/man1/fetch.1
	$(INSTALL_DATA) $(srcdir)/man/build-simh.1 $(DESTDIR)$(mandir)/man1/build-simh.1
	$(LN_S) -f $(prebsddir)/prebsd      $(DESTDIR)$(bindir)/prebsd
	$(LN_S) -f $(prebsddir)/fetch       $(DESTDIR)$(bindir)/prebsd-fetch
	$(LN_S) -f $(prebsddir)/build-simh  $(DESTDIR)$(bindir)/prebsd-build-simh

uninstall-hook:
	rm -rf $(DESTDIR)$(prebsddir)
	rm -f $(DESTDIR)$(bindir)/prebsd $(DESTDIR)$(bindir)/prebsd-fetch \
	      $(DESTDIR)$(bindir)/prebsd-build-simh \
	      $(DESTDIR)$(mandir)/man1/prebsd.1 $(DESTDIR)$(mandir)/man1/fetch.1 \
	      $(DESTDIR)$(mandir)/man1/build-simh.1

EXTRA_DIST = vax pdp11 pdp10 ini man simh simh.json images.json fetch build-simh mirror-built mirror-fill
