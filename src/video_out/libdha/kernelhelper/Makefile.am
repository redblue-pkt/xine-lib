
EXTRA_DIST = README dhahelper.c

CFLAGS = -O2 -Wall -D__KERNEL__ -DMODULE

KVERSION = $(shell $(SHELL) -c 'uname -r')
moddir = /lib/modules/$(KVERSION)/misc

KCOMPILE = $(CC) $(CFLAGS) $(INCLUDES) $(LINUX_INCLUDE)

if HAVE_LINUX
KERNEL_MODULE = dhahelper.o
endif

noinst_HEADERS = dhahelper.h

EXTRA_PROGRAMS = test
test_SOURCES = test.c

dhahelper.o:
	$(KCOMPILE) -c `test -f $*.c || echo '$(srcdir)/'`$*.c

nodes:
	$(MKNOD) -m 666 /dev/dhahelper c 180 0

all: $(KERNEL_MODULE)

install-exec-local: $(KERNEL_MODULE)
	@$(NORMAL_INSTALL)
	$(mkinstalldirs) $(DESTDIR)$(moddir)
	@list='$(KERNEL_MODULE)'; \
	for p in $$list; do \
	  if test -f $$p; then \
	    echo "$(INSTALL) -o root -g root -m 644 $$p $(DESTDIR)$(moddir)/$$p"; \
	    $(INSTALL) -o root -g root -m 644 $$p $(DESTDIR)$(moddir)/$$p; \
	  else :; fi; \
	done; \
	$(DEPMOD) -a
	if test ! -c /dev/dhahelper; then \
	  $(MAKE) nodes; \
	fi


uninstall-local:
	@$(NORMAL_UNINSTALL)
	@list='$(KERNEL_MODULE)'; \
	for p in $$list; do \
	  echo "rm -f $(DESTDIR)$(moddir)/`echo $$p|sed 's/$(EXEEXT)$$//'|sed '$(transform)'|sed 's/$$/$(EXEEXT)/'`"; \
	  rm -f $(DESTDIR)$(moddir)/`echo $$p|sed 's/$(EXEEXT)$$//'|sed '$(transform)'|sed 's/$$/$(EXEEXT)/'`; \
	done

debug:
install-debug: install

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#* test

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
