EXTRA_DIST = README dhahelper.c

CFLAGS = -O2 -Wall -D__KERNEL__ -DMODULE

## CLEAN ME
KVERSION = $(shell $(SHELL) -c 'uname -r')
MISC_KDIR = /lib/modules/$(KVERSION)/misc

KCOMPILE = $(CC) $(CFLAGS) $(INCLUDES) $(LINUX_INCLUDE)

noinst_HEADERS = dhahelper.h

EXTRA_PROGRAMS = test
test_SOURCES = test.c

dhahelper.o:
	$(KCOMPILE) -c `test -f $< || echo '$(srcdir)/'`$<

install-data-local: @HAVE_LINUX_TRUE@ dhahelper.o
@HAVE_LINUX_TRUE@	if test ! -d $(MISC_KDIR); then \
@HAVE_LINUX_TRUE@	  mkdir -p $(MISC_KDIR); \
@HAVE_LINUX_TRUE@	fi
@HAVE_LINUX_TRUE@	$(INSTALL_DATA) dhahelper.o $(MISC_KDIR)/dhahelper.o
@HAVE_LINUX_TRUE@	$(DEPMOD) -a
@HAVE_LINUX_TRUE@	if test ! -c /dev/dhahelper; then \
@HAVE_LINUX_TRUE@	  $(MKNOD) -m 666 /dev/dhahelper c 180 0; \
@HAVE_LINUX_TRUE@	fi

uninstall-local:
@HAVE_LINUX_TRUE@	rm -f $(MISC_KDIR)/dhahelper.o

debug:
install-debug: install

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#* test

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
