# prebsd — fetch and run Research Unix V4-V7 and 32V on simh.
#
# A set of scripts (fetch, boot.py); nothing to compile.  The scripts, the
# images.json manifest, and the ini/ configs live together under
# $(libdir)/prebsd, so fetch and boot.py can locate them by their own path.
# The user-facing entry point is the `prebsd-fetch` symlink in $(bindir).

prebsddir = $(libdir)/prebsd

install-data-hook:
	mkdir -p $(DESTDIR)$(prebsddir)/ini $(DESTDIR)$(bindir) $(DESTDIR)$(mandir)/man1
	$(INSTALL_SCRIPT) $(srcdir)/fetch    $(DESTDIR)$(prebsddir)/fetch
	$(INSTALL_SCRIPT) $(srcdir)/boot.py  $(DESTDIR)$(prebsddir)/boot.py
	$(INSTALL_DATA)   $(srcdir)/images.json $(DESTDIR)$(prebsddir)/images.json
	for f in $(srcdir)/ini/*.ini; do \
		$(INSTALL_DATA) "$$f" $(DESTDIR)$(prebsddir)/ini/; \
	done
	$(INSTALL_DATA) $(srcdir)/man/fetch.1 $(DESTDIR)$(mandir)/man1/fetch.1
	$(INSTALL_DATA) $(srcdir)/man/boot.1  $(DESTDIR)$(mandir)/man1/boot.1
	$(LN_S) -f $(prebsddir)/fetch $(DESTDIR)$(bindir)/prebsd-fetch

uninstall-hook:
	rm -rf $(DESTDIR)$(prebsddir)
	rm -f $(DESTDIR)$(bindir)/prebsd-fetch \
	      $(DESTDIR)$(mandir)/man1/fetch.1 \
	      $(DESTDIR)$(mandir)/man1/boot.1

EXTRA_DIST = vax ini man images.json fetch boot.py
