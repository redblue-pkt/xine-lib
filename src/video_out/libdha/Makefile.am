EXTRA_DIST = README pci_db2c.awk

SUBDIRS = bin kernelhelper oth sysdep

CFLAGS = @CFLAGS@ @STATIC@

if HAVE_VIDIX
dha_lib = libdha.la
endif

noinst_LTLIBRARIES = $(dha_lib)

libdha_la_SOURCES = libdha.c mtrr.c pci.c pci_names.c mmi.c
libdha_la_DEPENDENCIES = pci_names.c

EXTRA_PROGRAMS = test

test_SOURCES = test.c
test_LDADD = $(top_builddir)/src/video_out/libdha/libdha.la

noinst_HEADERS = AsmMacros.h  libdha.h

## for OpenBSD LIBS += -li386
pci_names.c:
	$(AWK) -f pci_db2c.awk oth/pci.db

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) @STATIC@ -DXINE_COMPILE"

install-debug: debug
	@list='$(SUBDIRS)'; for subdir in $$list; do \
	  (cd $$subdir && $(MAKE) $@) || exit;\
	done;
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#* test
	-rm -f pci_dev_ids.c pci_ids.h pci_names.c pci_names.h pci_vendors.h

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
